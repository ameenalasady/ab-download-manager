package com.abdownloadmanager.service

import ir.amirab.downloader.connection.proxy.*
import kotlinx.serialization.json.*
import okhttp3.Interceptor
import okhttp3.Response
import okhttp3.HttpUrl.Companion.toHttpUrl
import java.io.IOException
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.attribute.PosixFilePermissions

/** Explicit application policy, NOT a sandbox against a hostile same-UID process.
 * Production-capable HTTP(S) is opt-in and only supports newly initialized profiles.
 * No discovery, conversion or implicit reuse of a legacy/installed profile.
 */
class RuntimePolicy private constructor(
    val profile: Path, val destination: Path, val destinationRoots: List<Path>,
    val isolated: Boolean, val redirects: Boolean, val proxy: ProxyStrategy,
    val hostProxies: Map<String, ProxyStrategy> = emptyMap(),
) {
    /** Exact canonical host match only: no suffix/wildcard/PAC or ambient system lookup. */
    fun proxyFor(url: String): ProxyStrategy = hostProxies[url.toHttpUrl().host] ?: proxy
    val usesExplicitProxy get() = proxy != ProxyStrategy.Direct || hostProxies.isNotEmpty()
    val mode get() = if (isolated) "isolated" else "explicit-runtime"
    fun validateUrl(value: String) {
        require(value.length in 1..2048 && value.none { it.isISOControl() }) { "Invalid HTTP URL" }
        val uri = java.net.URI(value)
        require(uri.userInfo == null && uri.fragment == null && uri.port != 15151) { "URL credentials, fragments and port 15151 are forbidden" }
        if (isolated) require(uri.scheme == "http" && uri.host == "127.0.0.1" && uri.port in 1..65535) {
            "Isolated service accepts only explicit loopback HTTP fixture ports (not 15151)"
        } else require(uri.scheme in listOf("http", "https") && !uri.host.isNullOrBlank() && (uri.port == -1 || uri.port in 1..65535)) {
            "Only HTTP(S) URLs without embedded credentials are supported"
        }
        value.toHttpUrl() // Also validate using the engine's parser.
    }
    fun validateDestination(path: Path) {
        val absolute = path.toAbsolutePath().normalize()
        require(path.isAbsolute && absolute == path && destinationRoots.any { absolute.startsWith(it) }) { "Destination is outside explicitly allowed roots" }
        noSymlinkAncestors(absolute)
        if (Files.exists(absolute, NOFOLLOW_LINKS)) {
            require(Files.isDirectory(absolute, NOFOLLOW_LINKS) || Files.isRegularFile(absolute, NOFOLLOW_LINKS)) { "Special destination files are forbidden" }
            if (Files.isRegularFile(absolute, NOFOLLOW_LINKS)) require((Files.getAttribute(absolute, "unix:nlink", NOFOLLOW_LINKS) as Number).toLong() == 1L) { "Hard-linked destination refused" }
        }
        // Never allow a destination to overwrite the service's own metadata/credentials.
        require(!absolute.startsWith(profile.resolve("app-data")) && absolute != profile &&
            absolute != profile.resolve("qt-service.json") && absolute != profile.resolve("qt-launch.lock")) { "Destination overlaps profile metadata" }
    }
    fun validateFolder(path: Path) {
        if (isolated) require(path == destination) { "Stored destination escapes fixture Downloads" }
        validateDestination(path)
        require(Files.isDirectory(path, NOFOLLOW_LINKS) && Files.isWritable(path)) { "Destination folder must already exist and be writable" }
    }
    /** Like [validateFolder], but creates a missing folder. Only ever creates folders beneath an
     * already existing, explicitly approved root, after the location itself passed validation. */
    fun ensureFolder(path: Path): Path {
        if (isolated) require(path == destination) { "Stored destination escapes fixture Downloads" }
        validateDestination(path)
        if (!Files.exists(path, NOFOLLOW_LINKS)) Files.createDirectories(path)
        noSymlinkAncestors(path)
        require(Files.isDirectory(path, NOFOLLOW_LINKS) && Files.isWritable(path)) { "Destination folder must be a writable directory" }
        return path
    }
    /** Runs before every engine request, including redirected requests, not merely Add. */
    fun networkInterceptor() = Interceptor { chain ->
        try { validateUrl(chain.request().url.toString()) } catch (e: Exception) { throw IOException("Request rejected by runtime policy", e) }
        chain.proceed(chain.request())
    }
    /** Manual redirects so arbitrary stored headers/cookies never leak across origins.
     * TLS downgrade is refused; cross-origin requests retain only range/representation headers.
     */
    fun redirectInterceptor() = Interceptor { chain ->
        var request = chain.request()
        var response: Response
        var hops = 0
        while (true) {
            response = chain.proceed(request)
            if (!redirects || response.code !in listOf(301, 302, 303, 307, 308)) break
            val target = response.header("Location")?.let { request.url.resolve(it) } ?: break
            response.close()
            if (++hops > 10) throw IOException("Too many redirects")
            try { validateUrl(target.toString()) } catch (e: Exception) { throw IOException("Redirect rejected by runtime policy", e) }
            if (request.url.isHttps && !target.isHttps) throw IOException("HTTPS downgrade refused")
            // The core chooses an OkHttp route per original request. Never follow a
            // redirect through the wrong proxy or silently fall back to direct. The
            // user may explicitly Add the new URL under its own reviewed route.
            if (proxyFor(request.url.toString()) != proxyFor(target.toString())) throw IOException("Redirect crosses explicit proxy policies; review the target separately")
            val sameOrigin = request.url.scheme == target.scheme && request.url.host == target.host && request.url.port == target.port
            request = if (sameOrigin) request.newBuilder().url(target).build() else {
                okhttp3.Request.Builder().url(target).apply {
                    for (header in listOf("Range", "Accept", "Accept-Encoding", "User-Agent")) request.header(header)?.let { header(header, it) }
                }.build()
            }
        }
        response
    }
    companion object {
        fun noSymlinkAncestors(path: Path) {
            var current: Path? = path.toAbsolutePath()
            while (current != null) {
                require(!Files.isSymbolicLink(current)) { "Symlinked runtime path refused" }
                current = current.parent
            }
        }
        fun isolated(profile: Path): RuntimePolicy = RuntimePolicy(profile, profile.resolve("Downloads"), listOf(profile.resolve("Downloads")), true, false, ProxyStrategy.Direct)
        fun load(file: Path): RuntimePolicy {
            noSymlinkAncestors(file)
            // An archived policy still embeds the SOURCE profile's absolute path. Never
            // let reading that copy accidentally start the source instead of the archive.
            ProfileCopies.requireNotQuarantined(file.toAbsolutePath().parent)
            require(Files.isRegularFile(file, NOFOLLOW_LINKS) && Files.size(file) <= 16384) { "Invalid runtime policy file" }
            privateOwned(file)
            val doc = Json.parseToJsonElement(Files.readString(file)).jsonObject
            val required = setOf("version", "profile", "profileId", "destination", "destinationRoots", "redirects", "proxy")
            require(doc.keys.containsAll(required) && (doc.keys - required).all { it == "hostProxies" }) { "Invalid runtime policy keys" }
            require(doc["version"]?.jsonPrimitive?.int == 1) { "Unsupported runtime policy" }
            fun path(key: String) = Path.of(doc.getValue(key).jsonPrimitive.content).also {
                require(it.isAbsolute && it.normalize() == it); noSymlinkAncestors(it)
            }
            val profile = path("profile")
            require(Files.isDirectory(profile, NOFOLLOW_LINKS)); privateOwned(profile)
            ProfileCopies.requireNotQuarantined(profile)
            val marker = profile.resolve("qt-runtime-profile.json")
            require(Files.isRegularFile(marker, NOFOLLOW_LINKS)); privateOwned(marker)
            require(Files.size(marker) <= 1024)
            val identity = Json.parseToJsonElement(Files.readString(marker)).jsonObject
            require(identity.keys == setOf("version", "profileId") && identity["version"]?.jsonPrimitive?.int == 1 &&
                identity["profileId"] == doc["profileId"] && doc["profileId"]!!.jsonPrimitive.content.matches(Regex("[a-f0-9-]{36}"))) { "Profile initialization identity mismatch" }
            require(!Files.exists(profile.resolve(".abdm-desktop-integration.properties"))) { "Integrated profiles are not supported" }
            Files.walk(profile).use { paths -> require(paths.noneMatch { Files.isSymbolicLink(it) }) { "Symlinks in runtime profile refused" } }
            val roots = doc.getValue("destinationRoots").jsonArray.map { Path.of(it.jsonPrimitive.content).also { root ->
                require(root.isAbsolute && root.normalize() == root && root.parent != null && Files.isDirectory(root, NOFOLLOW_LINKS)); noSymlinkAncestors(root)
                require(!profile.startsWith(root) && !root.startsWith(profile.resolve("app-data"))) { "Destination root must not contain profile metadata" }
            } }
            require(roots.size in 1..16 && roots.distinct().size == roots.size)
            fun parseProxy(proxyDoc: JsonObject): ProxyStrategy = when (proxyDoc["type"]?.jsonPrimitive?.content) {
                "direct" -> { require(proxyDoc.keys == setOf("type")); ProxyStrategy.Direct }
                "http", "socks" -> {
                    require(proxyDoc.keys.all { it in setOf("type", "host", "port", "username", "password") })
                    val host = proxyDoc.getValue("host").jsonPrimitive.content
                    val port = proxyDoc.getValue("port").jsonPrimitive.int
                    require(host.length in 1..253 && host.none { it.isWhitespace() || it.isISOControl() || it in "/\\@" } && port in 1..65535 && port != 15151)
                    val username = proxyDoc["username"]?.jsonPrimitive?.content
                    val password = proxyDoc["password"]?.jsonPrimitive?.content
                    require(listOfNotNull(username, password).all { it.length <= 4096 && it.none(Char::isISOControl) })
                    require(proxyDoc["type"]!!.jsonPrimitive.content != "socks" || (username == null && password == null)) { "Authenticated SOCKS is not supported" }
                    ProxyStrategy.ManualProxy(Proxy(if (proxyDoc["type"]!!.jsonPrimitive.content == "http") ProxyType.HTTP else ProxyType.SOCKS, host, port, username, password))
                }
                else -> error("Only explicit direct/HTTP/SOCKS proxy policies are supported")
            }
            val proxy = parseProxy(doc.getValue("proxy").jsonObject)
            val hostProxies = doc["hostProxies"]?.jsonObject?.mapValues { (host, policy) ->
                require(host.length in 1..253 && host.none { it.isWhitespace() || it.isISOControl() || it in "/\\@:*?#[]" } &&
                    "http://$host/".toHttpUrl().host == host && !host.endsWith('.')) { "Host proxy keys must be exact canonical lower-case hosts" }
                parseProxy(policy.jsonObject)
            }.orEmpty()
            require(hostProxies.size <= 32) { "Too many explicit host proxy rules" }
            return RuntimePolicy(profile, path("destination"), roots, false, doc.getValue("redirects").jsonPrimitive.boolean, proxy, hostProxies).also { it.ensureFolder(it.destination) }
        }
        private fun privateOwned(path: Path) {
            require(Files.getOwner(path) == java.nio.file.FileSystems.getDefault().userPrincipalLookupService.lookupPrincipalByName(System.getProperty("user.name"))) { "Runtime path owner mismatch" }
            require(Files.getPosixFilePermissions(path).none { it in PosixFilePermissions.fromString("---rwxrwx") }) { "Runtime policy/profile must be private" }
        }
    }
}
