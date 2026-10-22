package com.abdownloadmanager.service

import ir.amirab.downloader.downloaditem.http.HttpDownloadItem
import kotlinx.coroutines.*
import kotlinx.serialization.json.*
import okhttp3.OkHttpClient
import okhttp3.Request
import com.sun.net.httpserver.HttpServer
import java.net.InetSocketAddress
import java.nio.file.Files
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions
import java.util.UUID
import kotlin.test.*

class RuntimePolicyTest {
    private fun private(path: Path) = Files.setPosixFilePermissions(path, PosixFilePermissions.fromString(if (Files.isDirectory(path)) "rwx------" else "rw-------"))
    private fun fixture(root: Path): Path {
        val profile = Files.createDirectory(root.resolve("profile")); private(profile)
        val destination = Files.createDirectory(root.resolve("downloads"))
        val id = UUID.randomUUID().toString()
        Files.writeString(profile.resolve("qt-runtime-profile.json"), """{"version":1,"profileId":"$id"}"""); private(profile.resolve("qt-runtime-profile.json"))
        return profile.resolve("qt-runtime-policy.json").also {
            Files.writeString(it, buildJsonObject {
                put("version", 1); put("profileId", id); put("profile", profile.toString()); put("destination", destination.toString())
                put("destinationRoots", JsonArray(listOf(JsonPrimitive(destination.toString())))); put("redirects", true)
                put("proxy", buildJsonObject { put("type", "direct") })
            }.toString()); private(it)
        }
    }
    private fun command(op: String, vararg fields: Pair<String, JsonElement>) = JsonObject(mapOf("op" to JsonPrimitive(op), *fields))

    @Test fun isolatedPolicyRemainsLoopbackOnlyAndProductionRequiresExplicitIdentity() {
        val root = Files.createTempDirectory("abdm-runtime-policy-")
        try {
            val isolated = RuntimePolicy.isolated(root)
            isolated.validateUrl("http://127.0.0.1:12345/file")
            for (url in listOf("https://example.test/file", "http://127.0.0.1/file", "http://127.0.0.1:15151/file", "http://user:pw@127.0.0.1:12345/file", "http://127.0.0.1:12345/file#fragment"))
                assertFails { isolated.validateUrl(url) }
            val file = fixture(root); val runtime = RuntimePolicy.load(file)
            assertEquals("explicit-runtime", runtime.mode)
            runtime.validateUrl("https://example.test/file"); runtime.validateUrl("http://localhost:12345/file")
            for (url in listOf("ftp://example.test/file", "https://example.test:15151/file", "https://user:secret@example.test/file", "https://example.test/\nfile")) assertFails { runtime.validateUrl(url) }
            val before = Files.readString(file)
            Files.writeString(file, before.replace("\"profileId\":\"", "\"profileId\":\"0"))
            assertFails { RuntimePolicy.load(file) }
            Files.writeString(file, before)
            Files.setPosixFilePermissions(file, PosixFilePermissions.fromString("rw-r--r--"))
            assertFails { RuntimePolicy.load(file) }
            private(file)
            Files.createSymbolicLink(root.resolve("alias.json"), file)
            assertFails { RuntimePolicy.load(root.resolve("alias.json")) }
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun destinationsRejectEscapesSymlinksHardLinksAndMetadataRoots() {
        val root = Files.createTempDirectory("abdm-runtime-destination-")
        try {
            val file = fixture(root); val policy = RuntimePolicy.load(file)
            policy.validateDestination(policy.destination.resolve("safe.bin"))
            assertFails { policy.validateDestination(root.resolve("escape.bin")) }
            assertFails { policy.validateDestination(policy.destination.resolve("../escape.bin")) }
            Files.createSymbolicLink(policy.destination.resolve("alias"), root)
            assertFails { policy.validateDestination(policy.destination.resolve("alias/escape.bin")) }
            val outside = Files.writeString(root.resolve("outside"), "untouched")
            Files.createLink(policy.destination.resolve("hardlink"), outside)
            assertFails { policy.validateDestination(policy.destination.resolve("hardlink")) }
            assertEquals("untouched", Files.readString(outside))
            val original = Json.parseToJsonElement(Files.readString(file)).jsonObject
            Files.writeString(file, JsonObject(original + ("destinationRoots" to JsonArray(listOf(JsonPrimitive(root.toString()))))).toString())
            assertFails { RuntimePolicy.load(file) }
        } finally {
            // Kotlin File.deleteRecursively follows the deliberate alias -> root
            // cycle in this fixture. NIO walk defaults to NO FOLLOW_LINKS, so cleanup
            // only unlinks the generated alias and never traverses its target.
            Files.walk(root).use { paths -> paths.sorted(Comparator.reverseOrder()).forEach { Files.deleteIfExists(it) } }
        }
    }

    @Test fun explicitProxyPoliciesRejectImplicitSystemAndUnsupportedAuthentication() {
        val root = Files.createTempDirectory("abdm-runtime-proxy-")
        try {
            val file = fixture(root); val original = Json.parseToJsonElement(Files.readString(file)).jsonObject
            for (proxy in listOf("""{"type":"system"}""", """{"type":"http","host":"127.0.0.1","port":15151}""",
                """{"type":"socks","host":"127.0.0.1","port":12345,"username":"u","password":"p"}""")) {
                Files.writeString(file, JsonObject(original + ("proxy" to Json.parseToJsonElement(proxy))).toString())
                assertFails { RuntimePolicy.load(file) }
            }
            Files.writeString(file, JsonObject(original + ("proxy" to Json.parseToJsonElement("""{"type":"http","host":"127.0.0.1","port":12345,"username":"u","password":"p"}"""))).toString())
            assertFalse(RuntimePolicy.load(file).isolated)
        } finally { root.toFile().deleteRecursively() }
    }

    @Test fun exactHostRoutingHasNoAmbientFallbackAndCrossPolicyRedirectFailsClosed() {
        val root = Files.createTempDirectory("abdm-host-proxy-")
        val origin = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        try {
            val file = fixture(root)
            val original = Json.parseToJsonElement(Files.readString(file)).jsonObject
            fun writeRules(host: String, proxy: String) = Files.writeString(file, JsonObject(original + ("hostProxies" to
                buildJsonObject { put(host, Json.parseToJsonElement(proxy)) })).toString())
            writeRules("example.test", """{"type":"http","host":"127.0.0.1","port":12345,"username":"fixture-u","password":"fixture-p"}""")
            val policy = RuntimePolicy.load(file)
            assertTrue(policy.usesExplicitProxy)
            assertNotEquals(policy.proxyFor("http://example.test/a"), policy.proxyFor("http://other.test/a"))
            assertEquals(policy.proxy, policy.proxyFor("http://sub.example.test/a")) // NOT a suffix match.
            for (host in listOf("EXAMPLE.test", "*.example.test", "example.test.", "example.test:80", "example.test/path")) {
                writeRules(host, """{"type":"direct"}"""); assertFails { RuntimePolicy.load(file) }
            }
            for (proxy in listOf("""{"type":"system"}""", """{"type":"pac","url":"http://127.0.0.1:1/proxy.pac"}""",
                """{"type":"socks","host":"127.0.0.1","port":12345,"username":"u","password":"p"}""")) {
                writeRules("example.test", proxy); assertFails { RuntimePolicy.load(file) }
            }
            writeRules("localhost", """{"type":"http","host":"127.0.0.1","port":12345}""")
            val redirectPolicy = RuntimePolicy.load(file)
            origin.createContext("/redirect") { e ->
                e.responseHeaders.add("Location", "http://localhost:${origin.address.port}/never")
                e.sendResponseHeaders(302, -1); e.close()
            }
            origin.start()
            val client = OkHttpClient.Builder().followRedirects(false).addInterceptor(redirectPolicy.redirectInterceptor()).build()
            try {
                assertFails { client.newCall(Request.Builder().url("http://127.0.0.1:${origin.address.port}/redirect").build()).execute() }
            } finally { client.connectionPool.evictAll(); client.dispatcher.executorService.shutdown() }
        } finally { origin.stop(0); root.toFile().deleteRecursively() }
    }

    @Test fun httpOptionsValidateAtomicallyWithoutEchoingSecretsAndSurviveJoinedRestart() = runBlocking {
        val root = Files.createTempDirectory("abdm-runtime-engine-")
        val file = fixture(root); val policy = RuntimePolicy.load(file)
        val lifecycle = ServiceLifecycle(policy.profile, policy)
        try {
            var engine = lifecycle.start()
            val credentials = buildJsonObject { put("username", "fixture-user"); put("password", "private-password"); put("headers", buildJsonObject { put("Cookie", "fixture-secret") }) }
            val add = engine.command(command("add", "url" to JsonPrimitive("https://example.test/file"), "name" to JsonPrimitive("safe.bin"),
                "credentials" to credentials, "connections" to JsonPrimitive(2), "speedLimit" to JsonPrimitive("9007199254740993")))
            val id = add.getValue("id")
            val snapshot = engine.snapshot()
            assertFalse(snapshot.toString().contains("private-password")); assertFalse(snapshot.toString().contains("fixture-secret")); assertFalse(snapshot.toString().contains("fixture-user"))
            assertTrue(snapshot.getValue("rows").jsonArray.first().jsonObject.getValue("details").jsonObject.getValue("hasCredentials").jsonPrimitive.boolean)
            assertFails { engine.command(command("download.edit", "id" to id, "connections" to JsonPrimitive(0), "url" to JsonPrimitive("https://other.test/invalid"))) }
            assertEquals("https://example.test/file", engine.snapshot().getValue("rows").jsonArray.first().jsonObject.getValue("details").jsonObject.getValue("url").jsonPrimitive.content)
            engine.command(command("download.edit", "id" to id, "url" to JsonPrimitive("https://example.test/updated"), "connections" to JsonPrimitive(3)))
            lifecycle.stop()
            fun saved() = policy.profile.toFile().walkTopDown().filter { it.isFile && it.extension == "json" }.associate { it.path to (it.readText() to it.lastModified()) }
            val before = saved()
            ProfileLease.acquire(policy.profile.resolve("app-data/config/app.lock")).use { delay(600); assertEquals(before, saved()) }
            engine = lifecycle.start()
            val details = engine.snapshot().getValue("rows").jsonArray.first().jsonObject.getValue("details").jsonObject
            assertEquals("https://example.test/updated", details.getValue("url").jsonPrimitive.content)
            assertEquals(3, details.getValue("connections").jsonPrimitive.int)
            assertEquals("9007199254740993", details.getValue("speedLimit").jsonPrimitive.content)
            engine.command(command("download.edit", "id" to id, "credentials" to buildJsonObject {}))
            assertFalse(engine.snapshot().getValue("rows").jsonArray.first().jsonObject.getValue("details").jsonObject.getValue("hasCredentials").jsonPrimitive.boolean)
        } finally { lifecycle.stop(); root.toFile().deleteRecursively() }
    }

    @Test fun invalidHeadersAndCredentialsNeverMutateDraft() {
        for (credentials in listOf("""{"headers":{"Range":"bytes=0-"}}""", """{"headers":{"X-Test":"bad\r\nInjected: yes"}}""", """{"username":"u"}""", """{"headers":{"Cookie":"one","cookie":"two"}}""")) {
            val item = HttpDownloadItem("https://example.test/file", id = 1, folder = "/tmp", name = "x")
            assertFails { applyHttpOptions(item, buildJsonObject { put("credentials", Json.parseToJsonElement(credentials)) }) }
            assertNull(item.username); assertNull(item.headers)
        }
    }

    @Test fun redirectsStripSecretsAcrossOriginsAndIsolatedClientDoesNotFollow() {
        val root = Files.createTempDirectory("abdm-runtime-redirect-")
        val target = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        val origin = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        try {
            var authorization: String? = "not-called"; var cookie: String? = "not-called"; var custom: String? = "not-called"; var range: String? = null
            target.createContext("/file") { exchange ->
                authorization = exchange.requestHeaders.getFirst("Authorization"); cookie = exchange.requestHeaders.getFirst("Cookie"); custom = exchange.requestHeaders.getFirst("X-Secret"); range = exchange.requestHeaders.getFirst("Range")
                exchange.sendResponseHeaders(200, 2); exchange.responseBody.use { it.write("OK".toByteArray()) }
            }
            origin.createContext("/redirect") { exchange ->
                exchange.responseHeaders.add("Location", "http://127.0.0.1:${target.address.port}/file"); exchange.sendResponseHeaders(302, -1); exchange.close()
            }
            origin.createContext("/forbidden") { exchange ->
                exchange.responseHeaders.add("Location", "http://127.0.0.1:15151/never"); exchange.sendResponseHeaders(302, -1); exchange.close()
            }
            origin.start(); target.start()
            val policy = RuntimePolicy.load(fixture(root))
            val client = OkHttpClient.Builder().followRedirects(false).followSslRedirects(false).addInterceptor(policy.redirectInterceptor()).addNetworkInterceptor(policy.networkInterceptor()).build()
            client.newCall(Request.Builder().url("http://127.0.0.1:${origin.address.port}/redirect").header("Authorization", "secret").header("Cookie", "secret").header("X-Secret", "secret").header("Range", "bytes=0-1").build()).execute().use { assertEquals("OK", it.body!!.string()) }
            assertNull(authorization); assertNull(cookie); assertNull(custom); assertEquals("bytes=0-1", range)
            assertFails { client.newCall(Request.Builder().url("http://127.0.0.1:${origin.address.port}/forbidden").build()).execute() }
            val isolated = RuntimePolicy.isolated(root)
            val isolatedClient = OkHttpClient.Builder().followRedirects(false).addInterceptor(isolated.redirectInterceptor()).addNetworkInterceptor(isolated.networkInterceptor()).build()
            isolatedClient.newCall(Request.Builder().url("http://127.0.0.1:${origin.address.port}/redirect").build()).execute().use { assertEquals(302, it.code) }
            client.connectionPool.evictAll(); client.dispatcher.executorService.shutdown()
            isolatedClient.connectionPool.evictAll(); isolatedClient.dispatcher.executorService.shutdown()
        } finally { origin.stop(0); target.stop(0); root.toFile().deleteRecursively() }
    }
}
