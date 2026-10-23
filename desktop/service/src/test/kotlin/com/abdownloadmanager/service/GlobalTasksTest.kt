package com.abdownloadmanager.service

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import kotlin.test.*

class GlobalTasksTest {
    @Test fun globalQueueTasksAndStopAllDrainManualPendingAndActiveTransfers() = runBlocking {
        val root = Files.createTempDirectory("abdm-global-tasks-")
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val executor = Executors.newCachedThreadPool(); server.executor = executor
        server.createContext("/") { exchange ->
            try {
                val payload = ByteArray(1024 * 1024) { (it % 251).toByte() }
                val range = exchange.requestHeaders.getFirst("Range")?.removePrefix("bytes=")?.split('-')
                val start = range?.get(0)?.toIntOrNull() ?: 0
                val end = (range?.get(1)?.toIntOrNull() ?: payload.lastIndex).coerceAtMost(payload.lastIndex)
                if (range != null) exchange.responseHeaders.add("Content-Range", "bytes $start-$end/${payload.size}")
                exchange.responseHeaders.add("Accept-Ranges", "bytes")
                exchange.sendResponseHeaders(if (range == null) 200 else 206, (end - start + 1).toLong())
                exchange.responseBody.use { out ->
                    var offset = start
                    while (offset <= end) { val n = minOf(8192, end - offset + 1); out.write(payload, offset, n); out.flush(); offset += n; Thread.sleep(20) }
                }
            } catch (_: Exception) { } finally { exchange.close() }
        }
        server.start(); val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            suspend fun command(op: String) = engine.command(buildJsonObject { put("op", op) })
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject {
                put("maxConcurrentDownloads", 1); put("threadCount", 1); put("useSpeedLimit", "Enabled"); put("speedLimit", "65536")
            }) })
            engine.command(buildJsonObject { put("op", "queue.create"); put("name", "Second") })
            command("queue.startAll")
            assertTrue(engine.snapshot().getValue("queues").jsonArray.all { it.jsonObject.getValue("active").jsonPrimitive.boolean })
            command("queue.stopAll")
            assertTrue(engine.snapshot().getValue("queues").jsonArray.all { !it.jsonObject.getValue("active").jsonPrimitive.boolean })
            suspend fun add(name: String) = engine.command(buildJsonObject {
                put("op", "add"); put("url", "http://127.0.0.1:${server.address.port}/$name"); put("name", name); put("start", true)
            }).getValue("id").jsonPrimitive.content
            val first = add("first.bin"); val second = add("second.bin")
            withTimeout(10000) { while (engine.snapshot().getValue("manualPending").jsonArray.none { it.jsonPrimitive.content == second }) delay(20) }
            withTimeout(10000) { while (engine.snapshot().getValue("rows").jsonArray.none { it.jsonObject.getValue("state").jsonPrimitive.content == "downloading" }) delay(20) }
            assertFails { engine.command(buildJsonObject { put("op", "download.reset"); put("id", first) }) }
            command("queue.stopAll") // Named queues only: the manual transfer continues.
            assertTrue(engine.snapshot().getValue("rows").jsonArray.any { it.jsonObject.getValue("state").jsonPrimitive.content == "downloading" })
            command("download.stopAll")
            delay(500) // A freed manual slot must not refill after the acknowledgement.
            val snapshot = engine.snapshot()
            assertTrue(snapshot.getValue("manualPending").jsonArray.isEmpty())
            assertTrue(snapshot.getValue("queues").jsonArray.all { !it.jsonObject.getValue("active").jsonPrimitive.boolean })
            assertTrue(snapshot.getValue("rows").jsonArray.none { it.jsonObject.getValue("state").jsonPrimitive.content == "downloading" })
        } finally { lifecycle.stop(); server.stop(0); executor.shutdownNow(); executor.awaitTermination(5, TimeUnit.SECONDS); root.toFile().deleteRecursively() }
    }
}
