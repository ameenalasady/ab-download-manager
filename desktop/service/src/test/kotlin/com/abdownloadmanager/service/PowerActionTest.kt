package com.abdownloadmanager.service

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import java.net.InetSocketAddress
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions
import java.util.UUID
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.test.*

class PowerActionTest {
    private fun private(path: Path) = Files.setPosixFilePermissions(path, PosixFilePermissions.fromString(if (Files.isDirectory(path)) "rwx------" else "rw-------"))
    private fun explicitRuntime(root: Path): Pair<Path, RuntimePolicy> {
        val profile = Files.createDirectory(root.resolve("profile")); private(profile)
        val destination = Files.createDirectory(root.resolve("downloads"))
        val id = UUID.randomUUID().toString()
        Files.writeString(profile.resolve("qt-runtime-profile.json"), """{"version":1,"profileId":"$id"}"""); private(profile.resolve("qt-runtime-profile.json"))
        val policy = profile.resolve("qt-runtime-policy.json").also {
            Files.writeString(it, buildJsonObject {
                put("version", 1); put("profileId", id); put("profile", profile.toString()); put("destination", destination.toString())
                put("destinationRoots", JsonArray(listOf(JsonPrimitive(destination.toString())))); put("redirects", true)
                put("proxy", buildJsonObject { put("type", "direct") })
            }.toString()); private(it)
        }
        return profile to RuntimePolicy.load(policy)
    }
    private fun command(op: String, vararg fields: Pair<String, JsonElement>) = JsonObject(mapOf("op" to JsonPrimitive(op), *fields))
    private fun updateMain(power: JsonElement?) = command("queue.update", "queueId" to JsonPrimitive("0"), "name" to JsonPrimitive("Main"),
        "maxConcurrent" to JsonPrimitive(1), "stopOnEmpty" to JsonPrimitive(true), *listOfNotNull(power?.let { "powerAction" to it }).toTypedArray())

    @Test fun finishedQueuePowerActionWaitsForAnExplicitAnswerAndRunsOnce() = runBlocking {
        val root = Files.createTempDirectory("abdm-power-test-")
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/") { e ->
            e.responseHeaders.add("Content-Length", "4")
            e.sendResponseHeaders(200, if (e.requestMethod == "HEAD") -1 else 4)
            if (e.requestMethod != "HEAD") e.responseBody.use { it.write(byteArrayOf(1, 2, 3, 4)) }
            e.close()
        }
        server.start()
        val initiated = CopyOnWriteArrayList<String>()
        val (profile, runtime) = explicitRuntime(root)
        val lifecycle = ServiceLifecycle(profile, runtime, browserIntegration = false, power = { initiated += it; true })
        try {
            val engine = lifecycle.start()
            assertTrue("queue.powerAction" in EngineSession.capabilities)
            assertFails { engine.command(updateMain(JsonPrimitive("Restart"))) }
            assertFails { engine.command(updateMain(JsonPrimitive(1))) }
            engine.command(updateMain(JsonPrimitive("Shutdown")))
            suspend fun main() = engine.snapshot().getValue("queues").jsonArray.first { it.jsonObject.getValue("id").jsonPrimitive.content == "0" }.jsonObject
            assertEquals("Shutdown", main().getValue("powerAction").jsonPrimitive.content)
            // Stored in the original per-queue extra settings, readable by the original app.
            val extra = profile.resolve("app-data/config/download_db/extra_queue_settings").toFile().walkTopDown().filter { it.isFile }.map { it.readText() }.toList()
            assertTrue(extra.any { it.contains("\"powerActionTypeOnFinish\":\"Shutdown\"") })
            var count = 0
            suspend fun finishQueue() {
                val id = engine.command(command("add", "url" to JsonPrimitive("http://127.0.0.1:${server.address.port}/f${++count}.bin"),
                    "name" to JsonPrimitive("f$count.bin"))).getValue("id")
                engine.command(command("queue.add", "queueId" to JsonPrimitive("0"), "id" to id))
                engine.command(command("queue.start", "queueId" to JsonPrimitive("0")))
            }
            suspend fun pending() = engine.snapshot().getValue("powerAction").takeIf { it !is JsonNull }?.jsonObject
            finishQueue()
            val request = withTimeout(20_000) { var p = pending(); while (p == null) { delay(20); p = pending() }; p }
            assertEquals("Shutdown", request.getValue("type").jsonPrimitive.content)
            assertEquals("queueFinished", request.getValue("reason").jsonPrimitive.content)
            assertEquals("Main", request.getValue("queueName").jsonPrimitive.content)
            delay(1_000); assertTrue(initiated.isEmpty()) // Never runs by itself.
            val serial = request.getValue("serial")
            assertFails { engine.command(command("power.execute", "serial" to JsonPrimitive("999"))) }
            assertNotNull(pending())
            engine.command(command("power.execute", "serial" to serial))
            assertEquals(listOf("Shutdown"), initiated.toList())
            assertNull(pending())
            assertFails { engine.command(command("power.execute", "serial" to serial)) } // Answered once.
            assertEquals(1, initiated.size)
            // Cancelling clears it without acting.
            finishQueue()
            val second = withTimeout(20_000) { var p = pending(); while (p == null) { delay(20); p = pending() }; p }
            assertNotEquals(serial, second.getValue("serial"))
            engine.command(command("power.cancel", "serial" to second.getValue("serial")))
            assertNull(pending()); assertEquals(1, initiated.size)
            // Turned off: a finished queue requests nothing.
            engine.command(updateMain(JsonNull))
            assertEquals(JsonNull, main().getValue("powerAction"))
            finishQueue()
            withTimeout(20_000) { while (engine.snapshot().getValue("rows").jsonArray.count { it.jsonObject.getValue("state").jsonPrimitive.content == "completed" } < 3) delay(20) }
            delay(500); assertNull(pending()); assertEquals(1, initiated.size)
        } finally { lifecycle.stop(); server.stop(0); root.toFile().deleteRecursively() }
    }

    @Test fun isolatedFixturesRefusePowerActions() = runBlocking {
        val root = Files.createTempDirectory("abdm-power-isolated-test-")
        val initiated = CopyOnWriteArrayList<String>()
        val lifecycle = ServiceLifecycle(root, power = { initiated += it; true })
        try {
            val engine = lifecycle.start()
            assertFails { engine.command(updateMain(JsonPrimitive("Shutdown"))) }
            engine.command(updateMain(JsonNull)) // Turning it off is always allowed.
            assertFails { engine.command(command("power.execute", "serial" to JsonPrimitive("1"))) }
            assertEquals(JsonNull, engine.snapshot().getValue("powerAction"))
            assertTrue(initiated.isEmpty())
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
