package com.abdownloadmanager.service

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.delay
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.*
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
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

    @Test fun probeBatchAnswersAtOnceAndReportsPerItemResultsInSnapshots() = runBlocking {
        val root = Files.createTempDirectory("abdm-probe-batch-")
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val release = CountDownLatch(1)
        val requests = AtomicInteger()
        server.executor = Executors.newFixedThreadPool(8)
        server.createContext("/") { exchange ->
            requests.incrementAndGet()
            when (exchange.requestURI.path) {
                "/missing.zip" -> exchange.sendResponseHeaders(404, -1)
                "/hang.zip" -> { Thread.sleep(30_000); exchange.sendResponseHeaders(404, -1) }
                else -> {
                    if (exchange.requestURI.path == "/slow.zip") release.await(20, TimeUnit.SECONDS)
                    exchange.responseHeaders.add("Content-Range", "bytes 0-255/4096")
                    exchange.responseHeaders.add("Accept-Ranges", "bytes")
                    exchange.sendResponseHeaders(206, 256)
                    exchange.responseBody.use { it.write(ByteArray(256)) }
                }
            }
            exchange.close()
        }
        server.start(); val lifecycle = ServiceLifecycle(root)
        val base = "http://127.0.0.1:${server.address.port}"
        try {
            val engine = lifecycle.start()
            val id = engine.command(buildJsonObject { put("op", "links.review"); put("text", "$base/fast.zip\n$base/slow.zip\n$base/missing.zip") })
                .getValue("requestId").jsonPrimitive.content
            val journal = root.resolve("app-data/config/browser-review.json")
            val before = Files.readAllBytes(journal)
            suspend fun probe(vararg indices: Int) = engine.command(buildJsonObject {
                put("op", "browser.probeBatch"); put("requestId", id); put("indices", JsonArray(indices.map { JsonPrimitive(it) }))
            })
            suspend fun items() = engine.snapshot().getValue("browserRequests").jsonArray.single().jsonObject.getValue("items").jsonArray.map { it.jsonObject }
            suspend fun probeOf(index: Int) = items()[index]["probe"]?.jsonObject
            assertFails { probe(3) }; assertFails { probe(0, 0) }
            assertEquals(0, requests.get())
            val started = System.nanoTime()
            assertEquals(3, probe(0, 1, 2).getValue("queued").jsonPrimitive.int)
            assertTrue((System.nanoTime() - started) / 1_000_000 < 1_000, "probeBatch must not wait for the probes")
            assertEquals("pending", probeOf(1)!!.getValue("state").jsonPrimitive.content)
            withTimeout(10_000) { while (probeOf(0)?.get("state")?.jsonPrimitive?.content != "done" || probeOf(2)?.get("state")?.jsonPrimitive?.content != "done") delay(20) }
            assertEquals("4096", probeOf(0)!!.getValue("size").jsonPrimitive.content)
            assertEquals(404, probeOf(2)!!.getValue("statusCode").jsonPrimitive.int)
            // The slow link is still running; other commands and snapshots are not held up.
            assertEquals("pending", probeOf(1)!!.getValue("state").jsonPrimitive.content)
            assertEquals(0, probe(1).getValue("queued").jsonPrimitive.int) // Already running: not probed twice.
            release.countDown()
            withTimeout(10_000) { while (probeOf(1)?.get("state")?.jsonPrimitive?.content != "done") delay(20) }
            assertEquals(3, requests.get())
            assertContentEquals(before, Files.readAllBytes(journal)) // Probing never rewrites the review.
            assertTrue(items().none { it.toString().contains("Content-Range") })
            // Partial acceptance keeps the remaining items' results.
            engine.command(buildJsonObject {
                put("op", "browser.accept"); put("requestId", id); put("start", false); put("categoryId", "none")
                put("items", JsonArray(listOf(buildJsonObject { put("index", 0); put("url", "$base/fast.zip"); put("name", "fast.zip") })))
            })
            val left = items()
            assertEquals(listOf("$base/slow.zip", "$base/missing.zip"), left.map { it.getValue("url").jsonPrimitive.content })
            assertEquals("4096", left[0].getValue("probe").jsonObject.getValue("size").jsonPrimitive.content)
            assertEquals(404, left[1].getValue("probe").jsonObject.getValue("statusCode").jsonPrimitive.int)
            // Stopping the service cancels a probe that would otherwise run for 30 s.
            val hanging = engine.command(buildJsonObject { put("op", "links.review"); put("text", "$base/hang.zip") }).getValue("requestId").jsonPrimitive.content
            engine.command(buildJsonObject { put("op", "browser.probeBatch"); put("requestId", hanging); put("indices", JsonArray(listOf(JsonPrimitive(0)))) })
            withTimeout(5_000) { while (requests.get() < 4) delay(20) }
            val stopping = System.nanoTime()
            lifecycle.stop()
            assertTrue((System.nanoTime() - stopping) / 1_000_000 < 5_000, "stop must cancel running probes")
        } finally { lifecycle.stop(); server.stop(0); (server.executor as java.util.concurrent.ExecutorService).shutdownNow(); root.toFile().deleteRecursively() }
    }
}
