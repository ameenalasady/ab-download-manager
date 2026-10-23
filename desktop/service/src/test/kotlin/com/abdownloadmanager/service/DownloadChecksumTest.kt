package com.abdownloadmanager.service

import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import java.nio.file.Files
import java.security.MessageDigest
import kotlin.test.*

class DownloadChecksumTest {
    private fun request(id: String, action: String, algorithm: String = "SHA-256", expected: JsonElement = JsonNull) = buildJsonObject {
        put("op", "download.checksum"); put("id", id); put("action", action); put("algorithm", algorithm); put("expected", expected)
    }
    @Test fun calculatesAllOriginalAlgorithmsAndPersistsOnlyExplicitExpectedHashes() = runBlocking {
        val root = Files.createTempDirectory("abdm-checksum-")
        val lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            val id = engine.command(buildJsonObject { put("op", "add"); put("url", "http://127.0.0.1:12345/not-fetched"); put("name", "fixture.bin") })
                .getValue("id").jsonPrimitive.content
            assertFails { engine.command(request(id, "start")) } // Incomplete, never hashes partials.
            lifecycle.stop()
            val record = root.resolve("app-data/config/download_db/downloadlist/$id.json")
            val item = Json.parseToJsonElement(Files.readString(record)).jsonObject
            val bytes = "disposable checksum fixture\n".toByteArray()
            Files.write(root.resolve("Downloads/fixture.bin"), bytes)
            Files.writeString(record, JsonObject(item + mapOf("status" to JsonPrimitive("Completed"), "contentLength" to JsonPrimitive(bytes.size))).toString())
            engine = lifecycle.start()
            suspend fun state() = engine.snapshot().getValue("checksum").jsonObject
            for (algorithm in listOf("MD5", "SHA-1", "SHA-256", "SHA-512")) {
                val before = Files.readString(record)
                engine.command(request(id, "start", algorithm))
                withTimeout(5000) { while (state().getValue("state").jsonPrimitive.content == "running") delay(10) }
                assertEquals("done", state().getValue("state").jsonPrimitive.content)
                val hash = MessageDigest.getInstance(algorithm).digest(bytes).joinToString("") { "%02x".format(it) }
                assertEquals(hash, state().getValue("digest").jsonPrimitive.content)
                assertEquals(before, Files.readString(record)) // Calculation is read-only.
                engine.command(request(id, "save", algorithm, JsonPrimitive(hash.uppercase())))
                assertEquals("$algorithm:$hash", engine.snapshot().getValue("rows").jsonArray.single().jsonObject.getValue("details").jsonObject.getValue("fileChecksum").jsonPrimitive.content)
            }
            for (name in listOf("renamed.bin", "fixture.bin")) engine.command(buildJsonObject {
                put("op", "download.move"); put("id", id); put("name", name); put("folder", root.resolve("Downloads").toString())
            })
            assertEquals(JsonNull, engine.snapshot().getValue("checksum")) // Mutations retire calculated results, not saved expectations.
            val saved = Files.readString(record)
            for (bad in listOf(request(id, "save", "SHA-256", JsonPrimitive("abc")), request(id, "save", "SHA-256", JsonPrimitive(3)),
                request(id, "start", "SHA-999"), request(id, "unknown"))) {
                assertFails { engine.command(bad) }; assertEquals(saved, Files.readString(record))
            }
            lifecycle.stop(); engine = lifecycle.start()
            assertTrue(engine.snapshot().getValue("rows").jsonArray.single().jsonObject.getValue("details").jsonObject.getValue("fileChecksum").jsonPrimitive.content.startsWith("SHA-512:"))
            engine.command(request(id, "save"))
            assertEquals(JsonNull, engine.snapshot().getValue("rows").jsonArray.single().jsonObject.getValue("details").jsonObject.getValue("fileChecksum"))
            // A large sparse owned fixture keeps the worker busy without allocating a
            // giant byte array. Commands must stay responsive, and mutations refuse it.
            java.io.RandomAccessFile(root.resolve("Downloads/fixture.bin").toFile(), "rw").use { it.setLength(1024L * 1024 * 1024) }
            engine.command(request(id, "start", "SHA-512"))
            assertEquals("running", state().getValue("state").jsonPrimitive.content)
            for (op in listOf("download.reset", "download.forget")) {
                assertFails { engine.command(buildJsonObject { put("op", op); put("id", id); put("keepFiles", true) }) }
            }
            assertFails { engine.command(buildJsonObject {
                put("op", "download.move"); put("id", id); put("name", "moved.bin"); put("folder", root.resolve("Downloads").toString())
            }) }
            assertFails { engine.command(buildJsonObject {
                put("op", "add"); put("url", "http://127.0.0.1:12345/not-fetched"); put("name", "fixture.bin"); put("duplicatePolicy", "overwrite")
            }) }
            assertTrue(Files.isRegularFile(root.resolve("Downloads/fixture.bin")))
            assertFails { engine.command(request(id, "start")) }
            engine.command(request(id, "cancel"))
            assertEquals("cancelled", state().getValue("state").jsonPrimitive.content)
            assertEquals("", state().getValue("digest").jsonPrimitive.content)
            val settled = state(); delay(100); assertEquals(settled, state())
            engine.command(request(id, "start", "SHA-512"))
            lifecycle.stop() // Joins a running reader before releasing the lease.
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).use { }
            engine = lifecycle.start()
            assertEquals(JsonNull, engine.snapshot().getValue("checksum")) // No automatic restart/replay.
            Files.delete(root.resolve("Downloads/fixture.bin"))
            assertFails { engine.command(request(id, "start")) }
            Files.createSymbolicLink(root.resolve("Downloads/fixture.bin"), record)
            assertFails { engine.command(request(id, "start")) }
            Unit
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
