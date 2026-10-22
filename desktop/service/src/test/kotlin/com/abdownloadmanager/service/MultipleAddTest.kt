package com.abdownloadmanager.service

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.atomic.AtomicInteger
import kotlin.test.*

class MultipleAddTest {
    @Test fun manualLinksRemainReviewOnlyAndProbeUsesBoundedEngineMetadata() = runBlocking {
        val root = Files.createTempDirectory("abdm-multiple-add-")
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val requests = AtomicInteger()
        server.createContext("/") { exchange ->
            requests.incrementAndGet()
            exchange.responseHeaders.add("Content-Range", "bytes 0-255/4096")
            exchange.responseHeaders.add("Accept-Ranges", "bytes")
            exchange.sendResponseHeaders(206, 256)
            exchange.responseBody.use { it.write(ByteArray(256)) }; exchange.close()
        }
        server.start(); var lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            val first = "http://127.0.0.1:${server.address.port}/first.zip"
            val second = "http://127.0.0.1:${server.address.port}/second.zip"
            val result = engine.command(buildJsonObject { put("op", "links.review"); put("text", "$first\n$second\n$first") })
            val id = result.getValue("requestId").jsonPrimitive.content
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty()); assertEquals(0, requests.get())
            lifecycle.stop(); lifecycle = ServiceLifecycle(root); engine = lifecycle.start()
            val pending = engine.snapshot().getValue("browserRequests").jsonArray.single().jsonObject
            assertEquals("clipboard", pending.getValue("source").jsonPrimitive.content) // Existing journal kind stays rollback-readable.
            assertEquals(2, pending.getValue("items").jsonArray.size)
            val journal = root.resolve("app-data/config/browser-review.json")
            val before = Files.readAllBytes(journal)
            assertFails { engine.command(buildJsonObject { put("op", "browser.probe"); put("requestId", id); put("index", 2) }) }
            assertEquals(0, requests.get())
            val metadata = engine.command(buildJsonObject { put("op", "browser.probe"); put("requestId", id); put("index", 1) })
            assertEquals("4096", metadata.getValue("size").jsonPrimitive.content)
            assertEquals(1, metadata.getValue("index").jsonPrimitive.int); assertEquals(1, requests.get())
            assertContentEquals(before, Files.readAllBytes(journal)) // Probe never reserves/rewrites the review.
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
            engine.command(buildJsonObject {
                put("op", "browser.accept"); put("requestId", id); put("start", false); put("categoryId", "none")
                put("items", JsonArray(listOf(buildJsonObject { put("index", 1); put("url", second); put("name", "renamed.zip") })))
            })
            val row = engine.snapshot().getValue("rows").jsonArray.single().jsonObject
            assertEquals("renamed.zip", row.getValue("name").jsonPrimitive.content)
            assertEquals(second, row.getValue("details").jsonObject.getValue("url").jsonPrimitive.content)
            assertEquals(1, requests.get()) // Add Paused never fetches.
            assertEquals(1, engine.snapshot().getValue("browserRequests").jsonArray.single().jsonObject.getValue("items").jsonArray.size)
        } finally { lifecycle.stop(); server.stop(0); root.toFile().deleteRecursively() }
    }
}
