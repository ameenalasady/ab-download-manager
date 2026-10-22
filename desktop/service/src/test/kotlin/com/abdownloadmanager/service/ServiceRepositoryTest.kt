package com.abdownloadmanager.service

import ir.amirab.downloader.DownloadSettings
import ir.amirab.downloader.db.TransactionalFileSaver
import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import java.nio.file.Files
import kotlin.test.*

class ServiceRepositoryTest {
    private fun command(op: String, vararg args: Pair<String, JsonElement>) = JsonObject(mapOf("op" to JsonPrimitive(op), *args))

    @Test fun legacySettingsRoundTripPreservesUnownedPropertiesAndRejectsPartialInvalidPatch() {
        val root = Files.createTempDirectory("abdm-settings-test-")
        try {
            val file = root.resolve("appSettings.json").toFile()
            file.writeText("""{"theme":"layan","apiEnabled":true,"unknownFuture":{"x":[1,2]},"minPartSize":524288,"threadCount":6}""")
            val repository = EngineSettingsRepository(file, TransactionalFileSaver(Json))
            val engine = DownloadSettings()
            repository.applyTo(engine)
            assertEquals(6, engine.defaultThreadCount)
            val before = file.readText()
            assertFailsWith<IllegalArgumentException> { repository.update(buildJsonObject { put("threadCount", 2); put("maxConcurrentDownloads", -1) }) }
            assertEquals(before, file.readText())
            assertEquals(6, repository.snapshot().getValue("threadCount").jsonPrimitive.int)
            assertFailsWith<IllegalArgumentException> { repository.update(buildJsonObject { put("apiEnabled", false) }) }
            repository.update(buildJsonObject { put("threadCount", 4); put("minPartSize", "1048576") })
            val stored = Json.parseToJsonElement(file.readText()).jsonObject
            assertEquals("layan", stored.getValue("theme").jsonPrimitive.content)
            assertTrue(stored.getValue("apiEnabled").jsonPrimitive.boolean)
            assertEquals(Json.parseToJsonElement("""{"x":[1,2]}"""), stored["unknownFuture"])
            assertFalse(stored.getValue("minPartSize").jsonPrimitive.isString)
            EngineSettingsRepository(file, TransactionalFileSaver(Json)).applyTo(engine)
            assertEquals(4, engine.defaultThreadCount); assertEquals(1048576L, engine.minPartSize)
            file.writeText("{corrupt")
            assertFails { EngineSettingsRepository(file, TransactionalFileSaver(Json)) }
            assertEquals("{corrupt", file.readText())
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun originalFixedSpeedAndMonitorPolicyValidateAtomicallyAndPersist() {
        val root = Files.createTempDirectory("abdm-speed-settings-test-")
        try {
            val file = root.resolve("appSettings.json").toFile()
            file.writeText("""{"theme":"layan","speedLimit":123456,"useSpeedLimit":"Enabled","useAverageSpeed":false}""")
            val repository = EngineSettingsRepository(file, TransactionalFileSaver(Json))
            val engine = DownloadSettings()
            repository.applyTo(engine); assertEquals(123456L, engine.globalSpeedLimit)
            assertFalse(repository.snapshot().getValue("useAverageSpeed").jsonPrimitive.boolean)
            val before = file.readText()
            for (invalid in listOf(buildJsonObject { put("speedLimit", "0") },
                buildJsonObject { put("speedLimit", "9223372036854775808") },
                buildJsonObject { put("threadCount", 2); put("useSpeedLimit", "EnabledWhenBessy") })) {
                assertFails { repository.update(invalid) }; assertEquals(before, file.readText())
            }
            repository.update(buildJsonObject { put("speedLimit", "9007199254740993"); put("useSpeedLimit", "Disabled"); put("useAverageSpeed", true) })
            repository.applyTo(engine); assertEquals(0L, engine.globalSpeedLimit)
            val reloaded = EngineSettingsRepository(file, TransactionalFileSaver(Json)).snapshot()
            assertEquals("9007199254740993", reloaded.getValue("speedLimit").jsonPrimitive.content)
            val stored = Json.parseToJsonElement(file.readText()).jsonObject
            assertFalse(stored.getValue("speedLimit").jsonPrimitive.isString)
            assertEquals("layan", stored.getValue("theme").jsonPrimitive.content)
            // A corrupt field falls back without rewriting the original document.
            file.writeText("""{"useSpeedLimit":"Sometimes"}""")
            val fallback = EngineSettingsRepository(file, TransactionalFileSaver(Json))
            assertEquals("Disabled", fallback.speedLimitMode())
            assertEquals(listOf("useSpeedLimit"), fallback.corruptKeys())
            assertEquals("""{"useSpeedLimit":"Sometimes"}""", file.readText())
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun existingSchedulerActuallyStartsAndStopsQueue() = runBlocking {
        val root = Files.createTempDirectory("abdm-scheduler-test-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            val now = java.time.LocalTime.now()
            engine.command(command("queue.schedule", "queueId" to JsonPrimitive("0"), "schedule" to buildJsonObject {
                put("daysOfWeek", JsonArray(listOf("MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY").map(::JsonPrimitive)))
                put("startTime", now.plusSeconds(4).withNano(0).toString())
                put("endTime", now.plusSeconds(6).withNano(0).toString())
                put("enabledStartTime", true); put("enabledEndTime", true)
            }))
            suspend fun active() = engine.snapshot().getValue("queues").jsonArray.first().jsonObject.getValue("active").jsonPrimitive.boolean
            withTimeout(12000) { while (!active()) delay(20); while (active()) delay(20) }
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun failedRepositoryBootDoesNotLeakWriterOrLease() = runBlocking {
        val root = Files.createTempDirectory("abdm-corrupt-queue-test-")
        val lifecycle = ServiceLifecycle(root)
        try {
            val file = root.resolve("app-data/config/download_db/queues/0.json").toFile()
            file.parentFile.mkdirs(); file.writeText("{corrupt queue")
            assertFails { lifecycle.start() }
            lifecycle.stop() // Failed-start cleanup is idempotent, not a closed-dispatcher reuse.
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).close()
            assertEquals("{corrupt queue", file.readText())
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun queuesSettingsImmediateShutdownRestartAndNoLateWriters() = runBlocking {
        val root = Files.createTempDirectory("abdm-queue-test-")
        val lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            assertFailsWith<IllegalStateException> { ProfileLease.acquire(root.resolve("app-data/config/app.lock")) }
            val created = engine.command(command("queue.create", "name" to JsonPrimitive("Nightly")))
            val id = created.getValue("queueId").jsonPrimitive.content
            assertEquals("11", id)
            engine.command(command("queue.update", "queueId" to JsonPrimitive(id), "name" to JsonPrimitive("Updated"),
                "maxConcurrent" to JsonPrimitive(4), "stopOnEmpty" to JsonPrimitive(true)))
            assertFails { engine.command(command("queue.update", "queueId" to JsonPrimitive(id), "name" to JsonPrimitive("Not applied"),
                "maxConcurrent" to JsonPrimitive(0), "stopOnEmpty" to JsonPrimitive(false))) }
            val futureTime = java.time.LocalTime.now().plusHours(1).withSecond(0).withNano(0).toString()
            val schedule = buildJsonObject {
                put("daysOfWeek", JsonArray(listOf("MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY").map(::JsonPrimitive)))
                put("startTime", futureTime); put("endTime", futureTime)
                put("enabledStartTime", true); put("enabledEndTime", true)
            }
            engine.command(command("queue.schedule", "queueId" to JsonPrimitive(id), "schedule" to schedule))
            assertFails { engine.command(command("queue.schedule", "queueId" to JsonPrimitive(id),
                "schedule" to JsonObject(schedule + ("startTime" to JsonPrimitive("25:99"))))) }
            engine.command(command("settings.update", "settings" to buildJsonObject { put("maxConcurrentDownloads", 1); put("threadCount", 3) }))
            engine.command(command("queue.start", "queueId" to JsonPrimitive("0")))
            lifecycle.stop() // No delay to let autosave "probably" finish.
            fun persisted() = root.toFile().walkTopDown().filter { it.isFile && it.extension == "json" }
                .associate { it.relativeTo(root.toFile()).path to (it.readText() to it.lastModified()) }
            val settled = persisted()
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).use { delay(800); assertEquals(settled, persisted()) }
            engine = lifecycle.start()
            val snapshot = engine.snapshot()
            val queue = snapshot.getValue("queues").jsonArray.map { it.jsonObject }.first { it["id"]?.jsonPrimitive?.content == id }
            assertEquals(schedule, queue["schedule"])
            assertEquals("Updated", queue["name"]?.jsonPrimitive?.content)
            assertEquals(4, queue["maxConcurrent"]?.jsonPrimitive?.int)
            assertFalse(queue.getValue("active").jsonPrimitive.boolean) // Active state isn't silently replayed.
            assertEquals(1, snapshot.getValue("settings").jsonObject.getValue("maxConcurrentDownloads").jsonPrimitive.int)
            assertFails { engine.command(command("queue.delete", "queueId" to JsonPrimitive("0"))) }
            engine.command(command("queue.delete", "queueId" to JsonPrimitive(id)))
            lifecycle.stop()
            assertFalse(root.resolve("app-data/config/download_db/queues/$id.json").toFile().exists())
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
