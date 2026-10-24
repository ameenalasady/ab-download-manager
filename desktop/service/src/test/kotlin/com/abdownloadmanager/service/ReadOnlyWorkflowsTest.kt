package com.abdownloadmanager.service

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.net.InetSocketAddress
import java.nio.file.Files
import kotlin.test.*

class ReadOnlyWorkflowsTest {
    @Test fun explicitProbeAndDuplicateReviewDoNotCreateJobsOrConsumeIds() = runBlocking {
        val root = Files.createTempDirectory("abdm-readonly-workflow-")
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        var calls = 0
        server.createContext("/") { e ->
            calls++
            assertEquals("bytes=0-255", e.requestHeaders.getFirst("Range"))
            e.responseHeaders.add("Content-Range", "bytes 0-255/4096")
            e.responseHeaders.add("Content-Disposition", "attachment; filename=fixture.bin")
            e.responseHeaders.add("Set-Cookie", "must-not-leak")
            e.sendResponseHeaders(206, 256); e.responseBody.use { it.write(ByteArray(256)) }; e.close()
        }
        server.start()
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            val url = "http://127.0.0.1:${server.address.port}/fixture.bin"
            fun request(op: String) = buildJsonObject { put("op", op); put("url", url); put("name", "fixture.bin") }
            val result = engine.command(request("download.probe"))
            assertEquals(1, calls)
            assertEquals("4096", result.getValue("size").jsonPrimitive.content)
            assertTrue(result.getValue("resumeSupport").jsonPrimitive.boolean)
            assertFalse(result.toString().contains("must-not-leak"))
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
            assertFalse(Files.exists(root.resolve("app-data/config/download_db/downloadlist/last_id.txt")))
            assertFalse(engine.command(request("download.review")).getValue("existingFile").jsonPrimitive.boolean)
            val add = engine.command(JsonObject(request("add") + ("duplicatePolicy" to JsonPrimitive("reject"))))
            val id = add.getValue("id").jsonPrimitive.content
            val before = Files.readAllBytes(root.resolve("app-data/config/download_db/downloadlist/$id.json"))
            val review = engine.command(request("download.review"))
            assertEquals(id, review.getValue("duplicates").jsonArray.single().jsonObject.getValue("id").jsonPrimitive.content)
            assertFails { engine.command(JsonObject(request("add") + ("duplicatePolicy" to JsonPrimitive("reject")))) }
            assertContentEquals(before, Files.readAllBytes(root.resolve("app-data/config/download_db/downloadlist/$id.json")))
            assertEquals(1, engine.snapshot().getValue("rows").jsonArray.size)
            assertEquals(1, calls) // Review/Add do not probe or start a download implicitly.
            val existing = root.resolve("Downloads/existing.bin")
            Files.writeString(existing, "keep-fixture")
            assertFails { engine.command(buildJsonObject {
                put("op", "add"); put("url", "$url?other=1"); put("name", "existing.bin"); put("duplicatePolicy", "reject")
            }) }
            assertEquals("keep-fixture", Files.readString(existing))
        } finally { lifecycle.stop(); server.stop(0); root.toFile().deleteRecursively() }
    }
    @Test fun probeReportsOnlyAWellFormedMediaTypeAndAddNamesBlankNamesFromTheLink() = runBlocking {
        val root = Files.createTempDirectory("abdm-readonly-workflow-")
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/") { e ->
            // Media type with parameters in mixed case, and one that is not a media type at all.
            e.responseHeaders.add("Content-Type", if (e.requestURI.path.endsWith("/vod")) "Video/MP4; charset=binary" else "weird text!!")
            e.responseHeaders.add("Content-Range", "bytes 0-255/4096")
            e.sendResponseHeaders(206, 256); e.responseBody.use { it.write(ByteArray(256)) }; e.close()
        }
        server.start()
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            val base = "http://127.0.0.1:${server.address.port}"
            suspend fun probe(path: String) = engine.command(buildJsonObject { put("op", "download.probe"); put("url", "$base$path") })
            val vod = probe("/MTc5Mjg0/vod")
            assertEquals("video/mp4", vod.getValue("contentType").jsonPrimitive.content)
            assertEquals("vod", vod.getValue("suggestedName").jsonPrimitive.content)
            assertEquals(JsonNull, probe("/other").getValue("contentType"))
            for (name in listOf<String?>(null, "", "  ")) {
                engine.command(buildJsonObject {
                    put("op", "add"); put("url", "$base/MTc5Mjg0/vod"); name?.let { put("name", it) }; put("duplicatePolicy", "numbered")
                })
            }
            val names = engine.snapshot().getValue("rows").jsonArray.map { it.jsonObject.getValue("name").jsonPrimitive.content }
            assertEquals(3, names.size)
            assertTrue(names.all { it == "vod" || it.startsWith("vod") }, names.toString())
        } finally { lifecycle.stop(); server.stop(0); root.toFile().deleteRecursively() }
    }
}
