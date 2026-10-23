package com.abdownloadmanager.service

import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.*
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermissions
import kotlin.test.*

class BrowserJournalTest {
    private fun item() = BrowserItem("http://127.0.0.1:12345/not-fetched", "fixture.bin", null,
        buildJsonObject { put("headers", buildJsonObject { put("Cookie", "fixture-secret") }) })

    @Test fun reviewAndCredentialsSurviveRestartWithoutReplay() {
        val root = Files.createTempDirectory("browser-journal-")
        try {
            val file = root.resolve("review.json")
            val first = BrowserIntake(journalFile = file)
            val id = first.offerItems(listOf(item()), 1, "browser").id
            assertEquals(PosixFilePermissions.fromString("rw-------"), Files.getPosixFilePermissions(file))
            val restarted = BrowserIntake(journalFile = file)
            assertEquals(id, restarted.requests.single().id)
            assertEquals("fixture-secret", restarted.get(id).items.single().credentials!!.getValue("headers").jsonObject.getValue("Cookie").jsonPrimitive.content)
            assertFalse(restarted.snapshot().toString().contains("fixture-secret"))
            restarted.reserve(id)
            val uncertain = BrowserIntake(journalFile = file)
            assertTrue(uncertain.requests.single().outcomeUnknown)
            assertFails { uncertain.get(id) }
            assertFails { uncertain.reserve(id) }
            uncertain.dismiss(id)
            assertTrue(BrowserIntake(journalFile = file).requests.isEmpty())
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun successfulSubsetPublishesOnlyUnselectedReviewItems() {
        val root = Files.createTempDirectory("browser-journal-")
        try {
            val file = root.resolve("review.json")
            val intake = BrowserIntake(journalFile = file)
            val id = intake.offerItems(listOf(item(), item()), 2, "clipboard").id
            intake.reserve(id); intake.complete(id, listOf(0))
            val restarted = BrowserIntake(journalFile = file)
            assertEquals(1, restarted.get(id).items.size)
            assertFalse(restarted.get(id).outcomeUnknown)
            restarted.reserve(id); restarted.complete(id, listOf(0))
            assertTrue(BrowserIntake(journalFile = file).requests.isEmpty())
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun interruptedWriteIsRetainedAndFailsClosed() {
        val root = Files.createTempDirectory("browser-journal-")
        try {
            val file = root.resolve("review.json")
            val intake = BrowserIntake(journalFile = file)
            val id = intake.offerItems(listOf(item()), 1, "browser").id
            val before = Files.readAllBytes(file)
            val temporary = root.resolve("review.json.tmp")
            Files.writeString(temporary, "unknown interrupted outcome")
            assertFails { intake.reserve(id) }
            assertFails { intake.get(id) }
            assertFails { BrowserIntake(journalFile = file) }
            assertContentEquals(before, Files.readAllBytes(file))
            assertEquals("unknown interrupted outcome", Files.readString(temporary))
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun actualPartialAddRetainsUnknownReservationAcrossRestart() = runBlocking {
        val root = Files.createTempDirectory("browser-partial-engine-")
        var lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            val url = "http://127.0.0.1:12345/not-fetched.zip"
            val offer = engine.command(buildJsonObject { put("op", "links.import"); put("text", url) })
            val id = offer.getValue("requestId").jsonPrimitive.content
            val obstruction = root.resolve("app-data/config/download_db/categories/categories.json.tmp")
            Files.createDirectory(obstruction) // Owned fixture fault, not a crashed writer.
            val accept = buildJsonObject {
                put("op", "browser.accept"); put("requestId", id); put("start", false)
                put("items", buildJsonArray { addJsonObject { put("index", 0); put("url", url); put("name", "partial.zip") } })
            }
            try { assertFails { engine.command(accept) } }
            finally { Files.delete(obstruction) } // Only this empty injected obstruction.
            assertEquals(1, engine.snapshot().getValue("rows").jsonArray.size)
            assertTrue(engine.snapshot().getValue("browserRequests").jsonArray.single().jsonObject.getValue("outcomeUnknown").jsonPrimitive.boolean)
            lifecycle.stop(); lifecycle = ServiceLifecycle(root); engine = lifecycle.start()
            assertFails { engine.command(accept) }
            assertEquals(1, engine.snapshot().getValue("rows").jsonArray.size)
            assertTrue(engine.snapshot().getValue("browserRequests").jsonArray.single().jsonObject.getValue("outcomeUnknown").jsonPrimitive.boolean)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun actualEngineRestoresReviewButNeverAddsOrStartsIt() = runBlocking {
        val root = Files.createTempDirectory("browser-journal-engine-")
        var lifecycle = ServiceLifecycle(root)
        try {
            var engine = lifecycle.start()
            val reply = engine.command(buildJsonObject { put("op", "links.import"); put("text", "http://127.0.0.1:12345/not-fetched") })
            val id = reply.getValue("requestId").jsonPrimitive.content
            lifecycle.stop(); lifecycle = ServiceLifecycle(root); engine = lifecycle.start()
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
            assertEquals(id, engine.snapshot().getValue("browserRequests").jsonArray.single().jsonObject.getValue("id").jsonPrimitive.content)
            engine.command(buildJsonObject { put("op", "browser.dismiss"); put("requestId", id) })
            lifecycle.stop(); lifecycle = ServiceLifecycle(root); engine = lifecycle.start()
            assertTrue(engine.snapshot().getValue("browserRequests").jsonArray.isEmpty())
            assertTrue(engine.snapshot().getValue("rows").jsonArray.isEmpty())
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }
}
