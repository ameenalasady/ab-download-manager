package com.abdownloadmanager.service

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.CopyOnWriteArrayList
import kotlin.test.*

class DesktopAdaptersTest {
    private class Notices : DesktopNotifier {
        override val available = true
        val sent = CopyOnWriteArrayList<DesktopNotice>()
        var closed = false
        override var onActivated: ((DesktopNotice, String?) -> Unit)? = null
        override fun notify(notice: DesktopNotice) { sent += notice }
        override fun close() { closed = true }
    }

    @Test fun activityEventsDriveTheBusyPolicyWithoutGuessingAndDrainOnStop() = runBlocking {
        val root = Files.createTempDirectory("abdm-activity-test-")
        val state = root.resolve("activity-state").toFile().apply { writeText("ready") }
        val helper = root.resolve("monitor.sh").toFile().apply {
            writeText("""
                last=''
                # Poll state, but stop promptly on stdin EOF like the real helper.
                # Bash read returns 142 on timeout and 1 on EOF. No TERM fallback.
                while IFS= read -r -t 0.05 unused || [ "${'$'}?" = 142 ]; do
                  state=${'$'}(head -c 32 "${'$'}1")
                  if [ "${'$'}state" != "${'$'}last" ]; then printf '%s\n' "${'$'}state"; last=${'$'}state; fi
                done
            """.trimIndent())
        }
        val launcher = FrontendLauncher(null)
        val lifecycle = ServiceLifecycle(root, launcher = launcher, activityCommand = listOf("/bin/bash", helper.path, state.path))
        try {
            val engine = lifecycle.start()
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("speedLimit", "1024"); put("useSpeedLimit", "EnabledWhenBessy") }) })
            suspend fun activity() = engine.snapshot().getValue("activity").jsonObject
            suspend fun limited() = engine.snapshot().getValue("settingsPolicy").jsonObject.getValue("speedLimitActive").jsonPrimitive.boolean
            withTimeout(5000) { while (!activity().getValue("available").jsonPrimitive.boolean) delay(20) }
            assertFalse(limited()) // ready is not evidence of user input.
            state.writeText("active")
            withTimeout(5000) { while (!limited() || activity().getValue("appliedSpeedLimit").jsonPrimitive.content != "1024") delay(20) }
            engine.frontendSeen(active = true)
            withTimeout(5000) { while (limited() || activity().getValue("appliedSpeedLimit").jsonPrimitive.content != "0") delay(20) }
            engine.frontendSeen(active = false)
            withTimeout(5000) { while (!limited()) delay(20) }
            state.writeText("idle")
            withTimeout(5000) { while (limited()) delay(20) }
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("useSpeedLimit", "Disabled") }) })
            assertEquals("stopped", activity().getValue("monitor").jsonPrimitive.content)
            assertFalse(activity().getValue("available").jsonPrimitive.boolean)
            lifecycle.stop()
            ProfileLease.acquire(root.resolve("app-data/config/app.lock")).close()
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun activityHelperThatIgnoresStdinEofIsStoppedByTheTermFallback() = runBlocking {
        val root = Files.createTempDirectory("abdm-activity-term-test-")
        val pidFile = root.resolve("helper.pid").toFile()
        // Never reads stdin, so closing it cannot stop the helper; only TERM can.
        val helper = root.resolve("monitor.sh").toFile().apply {
            writeText("""
                echo ${'$'}${'$'} > "${'$'}1"
                printf 'ready\n'
                while :; do sleep 0.05; done
            """.trimIndent())
        }
        val lifecycle = ServiceLifecycle(root, launcher = FrontendLauncher(null), activityCommand = listOf("/bin/sh", helper.path, pidFile.path))
        try {
            val engine = lifecycle.start()
            suspend fun activity() = engine.snapshot().getValue("activity").jsonObject
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("speedLimit", "1024"); put("useSpeedLimit", "EnabledWhenBessy") }) })
            withTimeout(5000) { while (!activity().getValue("available").jsonPrimitive.boolean || pidFile.length() == 0L) delay(20) }
            val pid = pidFile.readText().trim().toLong()
            assertTrue(ProcessHandle.of(pid).map { it.isAlive }.orElse(false))
            val started = System.nanoTime()
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("useSpeedLimit", "Disabled") }) })
            withTimeout(10_000) { while (activity().getValue("monitor").jsonPrimitive.content != "stopped") delay(20) }
            // The EOF grace period elapsed before TERM, and TERM actually ended the helper.
            assertTrue(System.nanoTime() - started >= 1_500_000_000L)
            withTimeout(5000) { while (ProcessHandle.of(pid).map { it.isAlive }.orElse(false)) delay(20) }
            assertFalse(activity().getValue("available").jsonPrimitive.boolean)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun clipboardReviewKeepsCredentialsServiceSideAndCompletionNotificationHonorsSound() = runBlocking {
        val root = Files.createTempDirectory("abdm-notice-test-")
        val notices = Notices()
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val authorization = CopyOnWriteArrayList<String>()
        server.createContext("/") { e ->
            authorization += e.requestHeaders.getFirst("Authorization") ?: "none"
            e.responseHeaders.add("Content-Length", "4")
            e.sendResponseHeaders(200, if (e.requestMethod == "HEAD") -1 else 4)
            if (e.requestMethod != "HEAD") e.responseBody.use { it.write(byteArrayOf(1, 2, 3, 4)) }
            e.close()
        }
        server.start()
        val lifecycle = ServiceLifecycle(root, notifier = { notices })
        try {
            val engine = lifecycle.start()
            engine.command(buildJsonObject { put("op", "settings.integration"); put("settings", buildJsonObject { put("notificationSound", false) }) })
            val link = "http://127.0.0.1:${server.address.port}/fixture.bin"
            val imported = engine.command(buildJsonObject { put("op", "links.import"); put("text", """{"link":"$link","username":"u","password":"secret-password"}""") })
            val snapshot = engine.snapshot()
            assertFalse(snapshot.toString().contains("secret-password"))
            assertTrue(snapshot.getValue("rows").jsonArray.isEmpty())
            val pending = snapshot.getValue("browserRequests").jsonArray.single().jsonObject
            assertEquals("clipboard", pending.getValue("source").jsonPrimitive.content)
            assertTrue(pending.getValue("items").jsonArray.single().jsonObject.getValue("hasHeaders").jsonPrimitive.boolean)
            val result = engine.command(buildJsonObject {
                put("op", "browser.accept"); put("requestId", imported.getValue("requestId")); put("start", true); put("categoryId", "none")
                put("items", buildJsonArray { addJsonObject { put("index", 0); put("url", link); put("name", "fixture.bin") } })
            })
            withTimeout(15_000) { while (notices.sent.isEmpty()) delay(20) }
            val notice = notices.sent.single()
            assertEquals("Finished", notice.body); assertFalse(notice.sound); assertFalse(notice.error)
            assertEquals(result.getValue("id").jsonPrimitive.content.toLong(), notice.downloadId)
            assertTrue(authorization.any { it == "Basic dTpzZWNyZXQtcGFzc3dvcmQ=" })
            notices.onActivated?.invoke(notice, "fresh-token")
            withTimeout(5000) {
                while (engine.snapshot().getValue("frontend").jsonObject.getValue("activation") is JsonNull) delay(20)
            }
            val activation = engine.snapshot().getValue("frontend").jsonObject.getValue("activation").jsonObject
            assertEquals("fresh-token", activation.getValue("token").jsonPrimitive.content)
            assertEquals(result.getValue("id"), activation["downloadId"])
            lifecycle.stop(); assertTrue(notices.closed)
            assertNull(notices.onActivated)
        } finally { lifecycle.stop(); server.stop(0); root.toFile().deleteRecursively() }
    }

    @Test fun downloadWindowHintsFollowUserStartsOnlyAreBoundedAndSettingsAreExecutable() = runBlocking {
        val root = Files.createTempDirectory("abdm-window-events-test-")
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/") { e ->
            if (e.requestURI.path.startsWith("/missing")) { e.sendResponseHeaders(404, -1); e.close(); return@createContext }
            e.responseHeaders.add("Content-Length", "4")
            e.sendResponseHeaders(200, if (e.requestMethod == "HEAD") -1 else 4)
            if (e.requestMethod != "HEAD") e.responseBody.use { it.write(byteArrayOf(1, 2, 3, 4)) }
            e.close()
        }
        server.start()
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            assertTrue("download.windows" in EngineSession.capabilities)
            suspend fun windows() = engine.snapshot().getValue("downloadWindows").jsonObject
            suspend fun events() = windows().getValue("events").jsonArray.map { it.jsonObject }
            assertEquals("0", windows().getValue("serial").jsonPrimitive.content); assertTrue(events().isEmpty())
            // Original defaults; executable integration keys, no longer "retained only".
            val integration = engine.snapshot().getValue("integration").jsonObject
            assertTrue(integration.getValue("showDownloadProgressDialog").jsonPrimitive.boolean)
            assertTrue(integration.getValue("showDownloadCompletionDialog").jsonPrimitive.boolean)
            assertTrue(engine.snapshot().getValue("settingsPolicy").jsonObject.getValue("retained").jsonArray.none {
                it.jsonObject.getValue("key").jsonPrimitive.content.startsWith("showDownload") })
            val base = "http://127.0.0.1:${server.address.port}"
            fun add(name: String, path: String = "/$name", start: Boolean = false) = buildJsonObject {
                put("op", "add"); put("url", base + path); put("name", name); put("start", start); put("categoryId", "none")
            }
            suspend fun state(id: String) = engine.snapshot().getValue("rows").jsonArray.single { it.jsonObject.getValue("id").jsonPrimitive.content == id }
                .jsonObject.getValue("state").jsonPrimitive.content
            // A queue start is not a user start: no window hint, as in the original.
            val queued = engine.command(add("queued.bin")).getValue("id").jsonPrimitive.content
            engine.command(buildJsonObject { put("op", "queue.add"); put("queueId", "0"); put("id", queued) })
            engine.command(buildJsonObject { put("op", "queue.start"); put("queueId", "0") })
            withTimeout(15_000) { while (state(queued) != "completed") delay(20) }
            engine.command(buildJsonObject { put("op", "queue.stop"); put("queueId", "0") })
            assertTrue(events().isEmpty())
            val started = engine.command(add("user.bin", start = true)).getValue("id").jsonPrimitive.content
            withTimeout(15_000) { while (events().none { it.getValue("kind").jsonPrimitive.content == "completed" }) delay(20) }
            assertEquals(listOf("started" to started, "completed" to started), events().map {
                it.getValue("kind").jsonPrimitive.content to it.getValue("id").jsonPrimitive.content })
            val failed = engine.command(add("missing.bin", "/missing", start = true)).getValue("id").jsonPrimitive.content
            withTimeout(15_000) { while (events().none { it.getValue("kind").jsonPrimitive.content == "failed" }) delay(20) }
            assertEquals(failed, events().last().getValue("id").jsonPrimitive.content)
            // Bounded ring; serials stay strictly increasing and the head serial is never reset.
            repeat(16) { engine.command(add("bulk-$it.bin", start = true)) }
            withTimeout(30_000) { while (events().count { it.getValue("kind").jsonPrimitive.content == "completed" } < 16) delay(20) }
            val serials = events().map { it.getValue("serial").jsonPrimitive.content.toLong() }
            assertEquals(EngineSession.MAX_WINDOW_EVENTS, serials.size)
            assertEquals(serials.sorted().distinct(), serials)
            assertEquals(windows().getValue("serial").jsonPrimitive.content.toLong(), serials.last())
            assertTrue(serials.last() >= 36)
            assertFalse(engine.snapshot().getValue("downloadWindows").toString().contains(base))
            // Settings apply all-or-nothing and persist with the original keys and types.
            val file = root.resolve("app-data/config/appSettings.json").toFile()
            assertFails { engine.command(buildJsonObject { put("op", "settings.integration"); put("settings", buildJsonObject {
                put("showDownloadProgressDialog", false); put("showDownloadCompletionDialog", "no") }) }) }
            assertTrue(engine.snapshot().getValue("integration").jsonObject.getValue("showDownloadProgressDialog").jsonPrimitive.boolean)
            engine.command(buildJsonObject { put("op", "settings.integration"); put("settings", buildJsonObject {
                put("showDownloadProgressDialog", false); put("showDownloadCompletionDialog", false) }) })
            val stored = Json.parseToJsonElement(file.readText()).jsonObject
            assertEquals(JsonPrimitive(false), stored["showDownloadProgressDialog"]); assertEquals(JsonPrimitive(false), stored["showDownloadCompletionDialog"])
            lifecycle.stop()
            // Memory-only: a new epoch starts an empty ring, settings survive.
            val restarted = lifecycle.start()
            assertEquals("0", restarted.snapshot().getValue("downloadWindows").jsonObject.getValue("serial").jsonPrimitive.content)
            assertFalse(restarted.snapshot().getValue("integration").jsonObject.getValue("showDownloadCompletionDialog").jsonPrimitive.boolean)
        } finally { lifecycle.stop(); server.stop(0); root.toFile().deleteRecursively() }
    }

    @Test fun pausingTheActiveManualDownloadStartsTheWaitingOneButNeverRestartsItself() = runBlocking {
        val root = Files.createTempDirectory("abdm-manual-slot-test-")
        // Keep the original first-transfer pause window; the second only needs to
        // complete after taking the freed slot, not stream for another 1.3 seconds.
        val firstBody = ByteArray(256 * 1024) { it.toByte() }
        val secondBody = ByteArray(64 * 1024) { it.toByte() }
        val server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val executor = java.util.concurrent.Executors.newCachedThreadPool()
        server.executor = executor
        server.createContext("/") { e ->
            val body = if (e.requestURI.path == "/first.bin") firstBody else secondBody
            e.responseHeaders.add("Content-Length", body.size.toString())
            e.sendResponseHeaders(200, if (e.requestMethod == "HEAD") -1 else body.size.toLong())
            if (e.requestMethod != "HEAD") runCatching {
                e.responseBody.use { out ->
                    body.toList().chunked(8192).forEach { chunk ->
                        out.write(chunk.toByteArray()); out.flush(); Thread.sleep(40)
                    }
                }
            }
            e.close()
        }
        server.start()
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start()
            engine.command(buildJsonObject { put("op", "settings.update"); put("settings", buildJsonObject { put("maxConcurrentDownloads", 1) }) })
            fun add(name: String) = buildJsonObject {
                put("op", "add"); put("url", "http://127.0.0.1:${server.address.port}/$name"); put("name", name); put("start", true); put("categoryId", "none")
            }
            val first = engine.command(add("first.bin")).getValue("id").jsonPrimitive.content
            val second = engine.command(add("second.bin")).getValue("id").jsonPrimitive.content
            suspend fun row(id: String) = engine.snapshot().getValue("rows").jsonArray.single { it.jsonObject.getValue("id").jsonPrimitive.content == id }.jsonObject
            suspend fun state(id: String) = row(id).getValue("state").jsonPrimitive.content
            withTimeout(10_000) { while (row(first).getValue("downloaded").jsonPrimitive.content.toLong() < 16384) delay(20) }
            assertEquals("queued", state(second))
            assertTrue(engine.snapshot().getValue("manualPending").jsonArray.any { it.jsonPrimitive.content == second })
            engine.command(buildJsonObject { put("op", "pause"); put("id", first) })
            withTimeout(10_000) { while (state(second) == "queued") delay(20) }
            withTimeout(15_000) { while (state(second) != "completed") delay(20) }
            delay(800) // Past the debounced refill: the user-paused download stays paused.
            assertEquals("paused", state(first))
        } finally {
            lifecycle.stop(); server.stop(0)
            executor.shutdownNow()
            check(executor.awaitTermination(5, java.util.concurrent.TimeUnit.SECONDS))
            root.toFile().deleteRecursively()
        }
    }

    @Test fun clipboardExtractionIsBoundedDeduplicatedAndDoesNotExecuteCommands() {
        val plain = "Notes https://example.invalid/a.zip and https://example.invalid/a.zip"
        assertEquals(listOf("https://example.invalid/a.zip"), ClipboardLinks.candidates(plain).map { it.link })
        val json = """[{"link":"https://example.invalid/a","headers":{"Cookie":"secret"}},{"link":"https://example.invalid/a"}]"""
        assertEquals("secret", ClipboardLinks.candidates(json).single().headers["Cookie"])
        val curl = """curl "https://example.invalid/a" -H "Cookie: secret""""
        assertEquals("secret", ClipboardLinks.candidates(curl).single().headers["Cookie"])
        val singleQuoted = "curl 'https://example.invalid/a' -H 'Cookie: secret' -u 'u:p:with:colons'"
        val parsed = ClipboardLinks.candidates(singleQuoted).single()
        assertEquals("secret", parsed.headers["Cookie"]); assertEquals("p:with:colons", parsed.password)
        assertEquals("https://example.invalid/a", ClipboardLinks.candidates("curl --url https://example.invalid/a --data-raw 'https://not-the-url.invalid/'").single().link)
        assertFails { ClipboardLinks.candidates("x".repeat(ClipboardLinks.MAX_TEXT + 1)) }
        val root = Files.createTempDirectory("abdm-clipboard-test-")
        try {
            val runtime = RuntimePolicy.isolated(root)
            val (accepted, ignored) = ClipboardLinks.items("""[{"link":"file:///etc/passwd"},{"link":"https://example.invalid/remote"},{"link":"http://127.0.0.1:12345/a"}]""", runtime)
            assertEquals(1, accepted.size); assertEquals(2, ignored)
        } finally { root.toFile().deleteRecursively() }
    }
}
