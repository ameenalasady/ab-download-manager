package com.abdownloadmanager.service

import com.sun.net.httpserver.HttpServer
import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import java.net.InetSocketAddress
import java.net.ServerSocket
import java.net.Socket
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.file.Files
import java.nio.file.Path
import java.util.concurrent.ConcurrentLinkedQueue
import kotlin.test.*

/** The original browser-extension contract (HTTP API + native-host hand-off) against the real engine. */
class BrowserIntegrationTest {
    private val payload = ByteArray(48 * 1024) { (it % 251).toByte() }
    private val received = ConcurrentLinkedQueue<Map<String, String>>()
    private val client = HttpClient.newHttpClient()

    private fun fixture(): HttpServer = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0).apply {
        createContext("/") { exchange ->
            received += exchange.requestHeaders.mapValues { it.value.joinToString(",") }.mapKeys { it.key.lowercase() }
            exchange.sendResponseHeaders(200, if (exchange.requestMethod == "HEAD") -1 else payload.size.toLong())
            if (exchange.requestMethod != "HEAD") exchange.responseBody.use { it.write(payload) }
            exchange.close()
        }
        start()
    }

    // The service needs a concrete port, so a test reserves one before the service binds it.
    // An ephemeral port (ServerSocket(0)) could be handed to another parallel test fork in
    // between. Instead pick from below Linux's ephemeral range (32768+), where nothing is
    // assigned automatically, and hold a cross-process lock file for the test's duration.
    private val reservedPorts = mutableListOf<java.nio.channels.FileChannel>()
    private fun freePort(): Int {
        val dir = Path.of(System.getProperty("java.io.tmpdir"), "abdm-test-ports"); Files.createDirectories(dir)
        repeat(500) {
            val port = 20000 + kotlin.random.Random.nextInt(10000)
            val channel = java.nio.channels.FileChannel.open(dir.resolve("$port.lock"),
                java.nio.file.StandardOpenOption.CREATE, java.nio.file.StandardOpenOption.WRITE)
            val lock = runCatching { channel.tryLock() }.getOrNull()
            if (lock != null && runCatching { ServerSocket(port).close() }.isSuccess) { reservedPorts += channel; return port }
            channel.close() // Also releases a lock taken on an occupied port.
        }
        error("No free test port")
    }
    @AfterTest fun releasePorts() { reservedPorts.forEach { it.close() }; reservedPorts.clear() }

    private fun profile(settings: String): Path = Files.createTempDirectory("abdm-browser-test-").also {
        val file = it.resolve("app-data/config/appSettings.json").toFile()
        file.parentFile.mkdirs(); file.writeText(settings)
    }

    private fun post(port: Int, path: String, body: String, key: String? = null, origin: String? = null): HttpResponse<String> {
        val builder = HttpRequest.newBuilder(URI("http://localhost:$port/$path")).POST(HttpRequest.BodyPublishers.ofString(body))
        key?.let { builder.header("X-Api-Key", it) }
        origin?.let { builder.header("Origin", it) }
        return client.send(builder.build(), HttpResponse.BodyHandlers.ofString())
    }

    private suspend fun EngineSession.rows() = snapshot().getValue("rows").jsonArray.map { it.jsonObject }

    private suspend fun EngineSession.awaitCompleted(count: Int) = withTimeout(30_000) {
        while (rows().count { it.getValue("state").jsonPrimitive.content == "completed" } < count) delay(50)
    }

    @Test fun headersAreReducedToEngineFieldsAndNamesAreMadeSafe() {
        val credentials = BrowserLinks.credentials(linkedMapOf("Cookie" to "sid=1", "Host" to "evil", "User-Agent" to "UA/1",
            "Range" to "bytes=0-", "Sec-Fetch-Mode" to "navigate", ":authority" to "x", "Referer" to "https://p/", "Bad" to "a\nb",
            "cookie" to "duplicate"))!!
        assertEquals("UA/1", credentials.getValue("userAgent").jsonPrimitive.content)
        assertEquals(setOf("Cookie", "Referer"), credentials.getValue("headers").jsonObject.keys)
        assertNull(BrowserLinks.credentials(mapOf("Host" to "x")))
        assertEquals("report.pdf", BrowserLinks.fileName("report.pdf", "http://h/x"))
        assertEquals("a b.zip", BrowserLinks.fileName(null, "http://h/dir/a%20b.zip?q=1"))
        assertEquals("_etc_passwd", BrowserLinks.fileName("/etc/passwd", "http://h/"))
        assertEquals("download", BrowserLinks.fileName("..", "http://h/"))
        val long = BrowserLinks.fileName("x".repeat(300) + ".tar", "http://h/")
        assertEquals(180, long.length); assertTrue(long.endsWith(".tar"))
    }

    @Test fun originalHttpApiAuthGuardsIntakeAndHandOffUseTheRealEngine() = runBlocking<Unit> {
        val server = fixture()
        val port = freePort()
        val root = profile("""{"apiEnabled":true,"apiPort":$port,"apiAuthEnabled":true,"apiAuthKey":"test-key","theme":"layan"}""")
        val lifecycle = ServiceLifecycle(root, browserIntegration = true)
        try {
            val engine = lifecycle.start(); engine.awaitBrowserBridge()
            val status = engine.snapshot().getValue("browserIntegration").jsonObject
            assertEquals("listening", status.getValue("state").jsonPrimitive.content)
            assertTrue(status.getValue("auth").jsonPrimitive.boolean)
            assertFalse(engine.snapshot().toString().contains("test-key"))
            // Original X-Api-Key auth.
            assertEquals(401, post(port, "ping", "null").statusCode())
            assertEquals(401, post(port, "ping", "null", key = "wrong").statusCode())
            assertEquals("pong", post(port, "ping", "null", key = "test-key").body())
            // Web pages and DNS rebinding cannot forge requests; extension origins can.
            val link = "http://127.0.0.1:${server.address.port}/files/archive.bin"
            val add = """{"items":[{"type":"http","link":"$link","headers":{"Cookie":"sid=secret-cookie","Host":"x","User-Agent":"Zen/1"},"suggestedName":"archive.bin","downloadPage":"http://127.0.0.1/page","description":null}],"options":{"silentAdd":false,"silentStart":false}}"""
            assertEquals(403, post(port, "add", add, key = "test-key", origin = "https://evil.example").statusCode())
            assertEquals(403, post(port, "add", add, key = "test-key", origin = "null").statusCode())
            Socket("127.0.0.1", port).use { socket ->
                socket.getOutputStream().write("POST /ping HTTP/1.1\r\nHost: attacker.example:$port\r\nX-Api-Key: test-key\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".toByteArray())
                assertTrue(socket.getInputStream().bufferedReader().readLine().contains("403"))
            }
            assertTrue(engine.snapshot().getValue("browserRequests").jsonArray.isEmpty())
            // GUI request: waits in the intake; cookies stay in the service.
            assertEquals("OK", post(port, "add", add, key = "test-key", origin = "moz-extension://1234").body())
            val requests = engine.snapshot().getValue("browserRequests").jsonArray
            assertEquals(1, requests.size)
            val pending = requests[0].jsonObject
            val item = pending.getValue("items").jsonArray[0].jsonObject
            assertEquals("archive.bin", item.getValue("name").jsonPrimitive.content)
            assertTrue(item.getValue("hasHeaders").jsonPrimitive.boolean)
            assertFalse(engine.snapshot().toString().contains("secret-cookie"))
            assertTrue(engine.rows().isEmpty())
            val requestId = pending.getValue("id").jsonPrimitive.content
            // A stale selection (URL mismatch) is refused before anything is added.
            assertFails { engine.command(buildJsonObject { put("op", "browser.accept"); put("requestId", requestId)
                put("items", buildJsonArray { addJsonObject { put("index", 0); put("url", "http://127.0.0.1:1/other") } }) }) }
            assertTrue(engine.rows().isEmpty())
            val accepted = engine.command(buildJsonObject { put("op", "browser.accept"); put("requestId", requestId); put("start", true)
                put("categoryId", "none"); put("items", buildJsonArray { addJsonObject { put("index", 0); put("url", link); put("name", "renamed.bin") } }) })
            assertTrue(accepted.getValue("ok").jsonPrimitive.boolean)
            engine.awaitCompleted(1)
            assertEquals(payload.toList(), Files.readAllBytes(root.resolve("Downloads/renamed.bin")).toList())
            val sent = received.last()
            assertEquals("sid=secret-cookie", sent["cookie"]); assertEquals("Zen/1", sent["user-agent"])
            assertEquals("127.0.0.1:${server.address.port}", sent["host"])
            assertTrue(engine.snapshot().getValue("browserRequests").jsonArray.isEmpty())
            assertTrue(engine.rows()[0].getValue("details").jsonObject.getValue("hasCredentials").jsonPrimitive.boolean)
            // Silent add + start, the legacy list payload, headless download and queue listing.
            assertEquals("OK", post(port, "add", """{"items":[{"type":"http","link":"$link?silent=1","suggestedName":null}],"options":{"silentAdd":true,"silentStart":true}}""", key = "test-key").body())
            engine.awaitCompleted(2)
            assertTrue(engine.rows().any { it.getValue("name").jsonPrimitive.content == "archive.bin" })
            assertEquals("OK", post(port, "add", """[{"link":"$link?legacy=1"}]""", key = "test-key").body())
            assertEquals(1, engine.snapshot().getValue("browserRequests").jsonArray.size)
            assertEquals("OK", post(port, "start-headless-download", """{"downloadSource":{"type":"http","link":"$link?h=1"},"name":"headless.bin","startDownload":true}""", key = "test-key").body())
            engine.awaitCompleted(3)
            val queues = client.send(HttpRequest.newBuilder(URI("http://127.0.0.1:$port/queues")).header("X-Api-Key", "test-key").GET().build(),
                HttpResponse.BodyHandlers.ofString()).body()
            assertTrue(Json.parseToJsonElement(queues).jsonArray.any { it.jsonObject.getValue("id").jsonPrimitive.long == 0L })
            // Unsupported/forbidden links fail the request and add nothing.
            val before = engine.rows().size
            assertNotEquals(200, post(port, "add", """{"items":[{"type":"hls","link":"$link.m3u8"}]}""", key = "test-key").statusCode())
            assertNotEquals(200, post(port, "add", """{"items":[{"type":"http","link":"http://127.0.0.1:15151/add"}],"options":{"silentAdd":true}}""", key = "test-key").statusCode())
            assertEquals(before, engine.rows().size)
            // Native-host hand-off path and explicit dismissal.
            val handOff = engine.command(buildJsonObject { put("op", "browser.add")
                put("request", Json.parseToJsonElement("""{"items":[{"type":"http","link":"$link?native=1","description":"x"}],"options":{}}""")) })
            assertTrue(handOff.getValue("requestId").jsonPrimitive.content.isNotEmpty())
            assertEquals(2, engine.snapshot().getValue("browserRequests").jsonArray.size)
            for (request in engine.snapshot().getValue("browserRequests").jsonArray)
                engine.command(buildJsonObject { put("op", "browser.dismiss"); put("requestId", request.jsonObject.getValue("id").jsonPrimitive.content) })
            assertTrue(engine.snapshot().getValue("browserRequests").jsonArray.isEmpty())
            assertEquals(before, engine.rows().size)
        } finally { lifecycle.stop(); server.stop(0); root.toFile().deleteRecursively() }
    }

    @Test fun portConflictIsReportedNotPersistedAndSettingsRebindWithWriteOnlyKey() = runBlocking<Unit> {
        // An unrelated but responsive occupant still causes a real bind conflict,
        // without spending a one-second read timeout on every identity probe.
        val occupied = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        occupied.createContext("/") { e -> e.sendResponseHeaders(404, -1); e.close() }
        occupied.start()
        try {
            val settings = """{"apiEnabled":true,"apiPort":${occupied.address.port}}"""
            val root = profile(settings)
            val lifecycle = ServiceLifecycle(root, browserIntegration = true)
            try {
                val engine = lifecycle.start(); engine.awaitBrowserBridge()
                val status = engine.snapshot().getValue("browserIntegration").jsonObject
                assertEquals("failed", status.getValue("state").jsonPrimitive.content)
                assertTrue(status.getValue("error").jsonPrimitive.content.contains("in use"))
                // Unlike the original, a coexisting app never flips the stored setting off.
                assertEquals(settings, root.resolve("app-data/config/appSettings.json").toFile().readText())
                val port = freePort()
                assertFails { engine.command(buildJsonObject { put("op", "browser.settings"); put("settings", buildJsonObject { put("apiAuthEnabled", true) }) }) }
                engine.command(buildJsonObject { put("op", "browser.settings"); put("settings", buildJsonObject {
                    put("apiPort", port); put("apiAuthKey", "rotated-key"); put("apiAuthEnabled", true) }) })
                engine.awaitBrowserBridge()
                assertEquals("listening", engine.snapshot().getValue("browserIntegration").jsonObject.getValue("state").jsonPrimitive.content)
                assertEquals(401, post(port, "ping", "null").statusCode())
                assertEquals(200, post(port, "ping", "null", key = "rotated-key").statusCode())
                assertFalse(engine.snapshot().toString().contains("rotated-key"))
                engine.command(buildJsonObject { put("op", "browser.settings"); put("settings", buildJsonObject { put("apiEnabled", false) }) })
                engine.awaitBrowserBridge()
                assertEquals("disabled", engine.snapshot().getValue("browserIntegration").jsonObject.getValue("state").jsonPrimitive.content)
                assertFails { post(port, "ping", "null", key = "rotated-key") }
            } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
        } finally { occupied.stop(0) }
    }

    @Test fun silentPortOccupantIsReportedAsAnotherApplicationAfterTheIdentityTimeout() = runBlocking<Unit> {
        // Accepts connections in the kernel backlog but never answers: the identity probe
        // must time out and report a foreign occupant, not hang or claim another ABDM.
        ServerSocket(0, 50, java.net.InetAddress.getByName("127.0.0.1")).use { occupied ->
            val settings = """{"apiEnabled":true,"apiPort":${occupied.localPort}}"""
            val root = profile(settings)
            val lifecycle = ServiceLifecycle(root, browserIntegration = true)
            try {
                val engine = lifecycle.start(); engine.awaitBrowserBridge()
                val status = engine.snapshot().getValue("browserIntegration").jsonObject
                assertEquals("failed", status.getValue("state").jsonPrimitive.content)
                assertTrue(status.getValue("error").jsonPrimitive.content.contains("another application"))
                assertEquals(settings, root.resolve("app-data/config/appSettings.json").toFile().readText())
                // Disabling cancels the pending bind retry (the stop would otherwise wait for it).
                engine.command(buildJsonObject { put("op", "browser.settings"); put("settings", buildJsonObject { put("apiEnabled", false) }) })
                engine.awaitBrowserBridge()
                assertEquals("disabled", engine.snapshot().getValue("browserIntegration").jsonObject.getValue("state").jsonPrimitive.content)
            } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
        }
    }

    @Test fun coexistenceRetriesOnlyBindingAndResumesWhenThePortIsReleased() = runBlocking {
        var calls = 0
        val handler = object : com.abdownloadmanager.integration.IntegrationHandler {
            override suspend fun addDownloadByGui(request: com.abdownloadmanager.integration.model.AddDownloadsFromIntegration) { calls++ }
            override fun listQueues() = emptyList<com.abdownloadmanager.integration.model.ApiQueueModel>()
            override suspend fun addDownload(task: com.abdownloadmanager.integration.model.NewDownloadTask): Long { calls++; return 1 }
        }
        val occupant = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        occupant.createContext("/ping") { e -> e.sendResponseHeaders(200, 4); e.responseBody.use { it.write("pong".toByteArray()) }; e.close() }
        occupant.start()
        val port = occupant.address.port
        val bridge = BrowserBridge(handler, true, retryMillis = 100)
        try {
            bridge.apply(BrowserSettings(true, port, null)); bridge.awaitIdle()
            val failed = bridge.snapshot()
            assertEquals("failed", failed.getValue("state").jsonPrimitive.content)
            assertEquals("abdm", failed.getValue("occupant").jsonPrimitive.content)
            assertTrue(failed.getValue("retrying").jsonPrimitive.boolean)
            assertTrue(failed.getValue("enabled").jsonPrimitive.boolean)
            occupant.stop(0)
            withTimeout(10_000) { while (bridge.snapshot().getValue("state").jsonPrimitive.content != "listening") delay(20) }
            assertEquals("pong", post(port, "ping", "null").body())
            assertEquals(0, calls) // No download/request was ever replayed by the bind retry.
            bridge.apply(BrowserSettings(false, port, null)); bridge.awaitIdle()
            assertEquals("disabled", bridge.snapshot().getValue("state").jsonPrimitive.content)
        } finally { bridge.close(); occupant.stop(0) }
    }

    @Test fun isolatedFixturesNeverBindTheBrowserPort() = runBlocking {
        val root = profile("""{"apiEnabled":true,"apiPort":${freePort()}}""")
        val lifecycle = ServiceLifecycle(root)
        try {
            val engine = lifecycle.start(); engine.awaitBrowserBridge()
            assertEquals("unavailable", engine.snapshot().getValue("browserIntegration").jsonObject.getValue("state").jsonPrimitive.content)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun frontendLauncherIsExplicitAbsoluteAndRateLimited() {
        assertFalse(FrontendLauncher.fromEnvironment(null).let { it.request(); it.connected })
        val marker = Files.createTempFile("abdm-launch-", ".txt")
        try {
            val launcher = FrontendLauncher.fromEnvironment("""["/bin/sh","-c","echo x >> $marker"]""")
            launcher.request(); launcher.request()
            Thread.sleep(500)
            assertEquals(1, Files.readAllLines(marker).size)
            launcher.seen(); assertTrue(launcher.connected)
            assertFalse(FrontendLauncher.fromEnvironment("""["sh"]""").let { it.request(); it.connected })
        } finally { Files.deleteIfExists(marker) }
    }
}
