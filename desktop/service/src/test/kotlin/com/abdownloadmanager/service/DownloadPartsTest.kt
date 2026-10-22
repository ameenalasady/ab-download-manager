package com.abdownloadmanager.service

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.Executors
import kotlin.test.*

class DownloadPartsTest {
    @Test fun wireSelectionRejectsNoncanonicalUnboundedOrRoundedIds() {
        assertEquals(emptyList(), decodePartsSelection(null))
        assertEquals(listOf(0L, 9007199254740993L, Long.MAX_VALUE), decodePartsSelection(JsonArray(listOf("0", "9007199254740993", "${Long.MAX_VALUE}").map(::JsonPrimitive))))
        for (bad in listOf("-1", "+1", "01", "1.0", "9223372036854775808", "", "not-an-id"))
            assertFails { decodePartsSelection(JsonArray(listOf(JsonPrimitive(bad)))) }
        assertFails { decodePartsSelection(JsonArray(listOf(JsonPrimitive(9007199254740993L)))) }
        assertFails { decodePartsSelection(JsonArray(listOf(JsonPrimitive("1"), JsonPrimitive("1")))) }
        assertFails { decodePartsSelection(JsonArray((0..16).map { JsonPrimitive("$it") })) }
        assertFails { decodePartsSelection(JsonNull) }
        assertFails { decodePartsSelection(buildJsonObject {}) }
    }

    @Test fun boundedStableIdProjectionUsesLoadedEnginePartsWithoutCredentialsOrMutation() = runBlocking {
        val root = Files.createTempDirectory("abdm-parts-bounded-")
        var lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            val id = engine.command(buildJsonObject {
                put("op", "add"); put("url", "http://127.0.0.1:12345/not-fetched"); put("name", "parts.bin")
                put("credentials", buildJsonObject { put("username", "parts-user-secret"); put("password", "parts-password-secret") })
            }).getValue("id").jsonPrimitive.content.toLong()
            lifecycle.stop()
            // A disposable engine fixture, loaded through the real PartListFileStorage.
            val db = root.resolve("app-data/config/download_db")
            val itemFile = db.resolve("downloadlist/$id.json")
            val json = Json.parseToJsonElement(Files.readString(itemFile)).jsonObject
            Files.writeString(itemFile, JsonObject(json + ("contentLength" to JsonPrimitive(3000))).toString())
            val partFile = db.resolve("parts/$id.json")
            Files.writeString(partFile, buildJsonObject {
                put("type", "ranges"); put("list", JsonArray((0 until 300).map { index -> buildJsonObject {
                    put("from", index * 10); put("to", index * 10 + 9); put("current", index * 10 + 5)
                } }))
            }.toString())
            lifecycle = ServiceLifecycle(root); engine = lifecycle.start()
            val before = Files.readAllBytes(partFile)
            assertTrue("download.parts" in EngineSession.capabilities)
            assertTrue(engine.snapshot().getValue("downloadParts").jsonObject.isEmpty())
            val parts = engine.snapshot(listOf(id)).getValue("downloadParts").jsonObject.getValue("$id").jsonObject
            assertTrue(parts.getValue("available").jsonPrimitive.boolean)
            assertEquals(300, parts.getValue("totalCount").jsonPrimitive.int)
            assertTrue(parts.getValue("truncated").jsonPrimitive.boolean)
            val ranges = parts.getValue("items").jsonArray
            assertEquals(256, ranges.size)
            assertEquals("2550", ranges.last().jsonObject.getValue("id").jsonPrimitive.content)
            assertEquals("5", ranges.first().jsonObject.getValue("downloaded").jsonPrimitive.content)
            assertEquals(setOf("id", "offset", "length", "downloaded", "state"), ranges.first().jsonObject.keys)
            assertFalse(parts.toString().contains("secret")); assertFalse(parts.toString().contains("not-fetched"))
            assertContentEquals(before, Files.readAllBytes(partFile)) // Reading never saves/resumes.
            assertFails { engine.snapshot(List(17) { it.toLong() }) }
            assertFails { engine.snapshot(listOf(id, id)) }
            assertFails { engine.snapshot(listOf(-1)) }
            val absent = engine.snapshot(listOf(Long.MAX_VALUE)).getValue("downloadParts").jsonObject.getValue("${Long.MAX_VALUE}").jsonObject
            assertFalse(absent.getValue("available").jsonPrimitive.boolean)
            assertTrue(absent.getValue("items").jsonArray.isEmpty())
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun realTransferPartsAdvancePauseResumeAndRetireOnCompletion() = runBlocking {
        val root = Files.createTempDirectory("abdm-parts-transfer-")
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val executor = Executors.newCachedThreadPool(); server.executor = executor
        val payload = ByteArray(2 * 1024 * 1024) { (it % 251).toByte() }
        server.createContext("/") { exchange ->
            try {
                val range = exchange.requestHeaders.getFirst("Range")?.removePrefix("bytes=")?.split('-')
                val start = range?.get(0)?.toIntOrNull() ?: 0
                val end = (range?.get(1)?.toIntOrNull() ?: payload.lastIndex).coerceAtMost(payload.lastIndex)
                if (range != null) exchange.responseHeaders.add("Content-Range", "bytes $start-$end/${payload.size}")
                exchange.responseHeaders.add("Accept-Ranges", "bytes")
                exchange.responseHeaders.add("Set-Cookie", "parts-response-secret")
                exchange.sendResponseHeaders(if (range == null) 200 else 206, (end - start + 1).toLong())
                exchange.responseBody.use { out ->
                    var offset = start
                    while (offset <= end) { val n = minOf(8192, end - offset + 1); out.write(payload, offset, n); out.flush(); offset += n; Thread.sleep(8) }
                }
            } catch (_: Exception) { } finally { exchange.close() }
        }
        server.start()
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject {
                put("threadCount", 2); put("minPartSize", "524288"); put("useSpeedLimit", "Enabled"); put("speedLimit", "262144")
            }) })
            val id = engine.command(buildJsonObject {
                put("op", "add"); put("url", "http://127.0.0.1:${server.address.port}/parts.bin"); put("name", "parts.bin"); put("start", true)
            }).getValue("id").jsonPrimitive.content.toLong()
            suspend fun projection() = engine.snapshot(listOf(id)).getValue("downloadParts").jsonObject.getValue("$id").jsonObject
            suspend fun progressed(): JsonObject = withTimeout(10000) {
                while (true) {
                    val p = projection()
                    if (p.getValue("items").jsonArray.sumOf { it.jsonObject.getValue("downloaded").jsonPrimitive.long } > 32768) return@withTimeout p
                    delay(30)
                }
                error("unreachable")
            }
            val active = progressed()
            assertTrue(active.getValue("available").jsonPrimitive.boolean)
            assertTrue(active.getValue("resumeSupport").jsonPrimitive.boolean)
            assertTrue(active.getValue("items").jsonArray.all { it.jsonObject.getValue("id").jsonPrimitive.isString })
            assertFalse(active.toString().contains("secret"))
            engine.command(buildJsonObject { put("op", "pause"); put("id", "$id") })
            withTimeout(10000) { while (engine.snapshot().getValue("rows").jsonArray.single().jsonObject.getValue("state").jsonPrimitive.content != "paused") delay(25) }
            val paused = projection(); assertTrue(paused.getValue("items").jsonArray.isNotEmpty())
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("useSpeedLimit", "Disabled") }) })
            engine.command(buildJsonObject { put("op", "resume"); put("id", "$id") })
            withTimeout(15000) { while (engine.snapshot().getValue("rows").jsonArray.single().jsonObject.getValue("state").jsonPrimitive.content != "completed") delay(25) }
            val done = projection()
            assertFalse(done.getValue("available").jsonPrimitive.boolean)
            assertTrue(done.getValue("items").jsonArray.isEmpty())
            assertContentEquals(payload, Files.readAllBytes(root.resolve("Downloads/parts.bin")))
        } finally { lifecycle.stop(); server.stop(0); executor.shutdown(); executor.awaitTermination(5, java.util.concurrent.TimeUnit.SECONDS); root.toFile().deleteRecursively() }
    }
}
