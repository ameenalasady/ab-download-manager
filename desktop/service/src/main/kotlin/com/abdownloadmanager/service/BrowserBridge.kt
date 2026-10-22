package com.abdownloadmanager.service

import com.abdownloadmanager.integration.IntegrationHandler
import com.abdownloadmanager.integration.IntegrationSettings
import com.abdownloadmanager.integration.MyServer
import com.abdownloadmanager.integration.createIntegrationServer
import kotlinx.serialization.json.*
import java.io.File
import java.net.BindException
import java.net.HttpURLConnection
import java.net.URI
import java.util.concurrent.RejectedExecutionException
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.ScheduledThreadPoolExecutor
import java.util.concurrent.TimeUnit

data class BrowserSettings(val enabled: Boolean, val port: Int, val apiKey: String?)

/** Owns the original browser-integration HTTP server (same endpoints, port and X-Api-Key
 * auth as the Compose app). (Re)binding runs on its own thread, never on the service
 * owner thread that in-flight requests wait for. A bind failure is reported, NOT persisted
 * as apiEnabled=false like the original: a coexisting app must not silently disable it.
 * While the port is busy the bind is retried every [retryMillis], so integration resumes
 * when the other instance quits. The occupant is identified only by the original `/ping`.
 */
class BrowserBridge(
    private val handler: IntegrationHandler, private val allowed: Boolean,
    private val retryMillis: Long = 15_000,
) : AutoCloseable {
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }
    // Shutdown drops a pending bind retry rather than waiting out its delay; queued
    // immediate work (the close cleanup) still runs.
    private val worker = ScheduledThreadPoolExecutor(1) { r -> Thread(r, "browser-bridge").apply { isDaemon = true } }.apply {
        removeOnCancelPolicy = true
        executeExistingDelayedTasksAfterShutdownPolicy = false
    }
    private var server: MyServer? = null
    private var retry: ScheduledFuture<*>? = null
    @Volatile private var status: JsonObject = state("disabled")
    @Volatile private var settings: BrowserSettings? = null

    private fun state(value: String, error: String? = null, occupant: String? = null, retrying: Boolean = false) = buildJsonObject {
        put("state", value); put("error", error?.let(::JsonPrimitive) ?: JsonNull)
        put("occupant", occupant?.let(::JsonPrimitive) ?: JsonNull); put("retrying", retrying)
    }

    /** Applies settings asynchronously; [awaitIdle] is for tests and orderly startup. */
    fun apply(next: BrowserSettings) {
        settings = next
        if (allowed && next.enabled) status = state("starting")
        worker.execute { bind(next) }
    }

    private fun bind(next: BrowserSettings) {
        retry?.cancel(false); retry = null
        if (settings !== next) return // Superseded by a newer apply().
        stopServer()
        status = when {
            !allowed -> state("unavailable", "Browser integration runs only with an explicit runtime profile")
            !next.enabled -> state("disabled")
            else -> try {
                createIntegrationServer(json, handler, IntegrationSettings(next.port, next.apiKey)).also {
                    server = it; it.startMyServer()
                }
                state("listening")
            } catch (e: Exception) {
                stopServer()
                val bind = generateSequence<Throwable>(e) { it.cause }.any { it is BindException }
                if (bind) {
                    val occupant = occupant(next.port)
                    // Closing (shutdown) refuses the retry; the bridge is then stopping anyway.
                    retry = try { worker.schedule({ bind(next) }, retryMillis, TimeUnit.MILLISECONDS) }
                        catch (_: RejectedExecutionException) { null }
                    // Never echo exception text: it can contain request data.
                    state("failed", if (occupant == "abdm") "Port ${next.port} is used by another AB Download Manager instance; retrying"
                        else "Port ${next.port} is already in use by another application; retrying", occupant, retrying = true)
                } else state("failed", "Browser integration could not start")
            }
        }
    }

    /** Best-effort identity only: exact original `/ping` response. 401 is not an identity. */
    private fun occupant(port: Int): String = runCatching {
        val connection = URI("http://127.0.0.1:$port/ping").toURL().openConnection() as HttpURLConnection
        connection.connectTimeout = 1000; connection.readTimeout = 1000; connection.instanceFollowRedirects = false
        connection.requestMethod = "POST"; connection.doOutput = true
        connection.outputStream.close()
        try {
            val code = connection.responseCode
            val body = if (code == 200) connection.inputStream.use { it.readNBytes(16).decodeToString() } else ""
            if (body == "pong") "abdm" else "other"
        } finally { connection.disconnect() }
    }.getOrDefault("other")

    fun awaitIdle() { worker.submit {}.get(30, TimeUnit.SECONDS) }

    fun snapshot(): JsonObject {
        val current = settings
        return buildJsonObject {
            status.forEach { (k, v) -> put(k, v) }
            put("enabled", current?.enabled ?: false); put("port", current?.port ?: 15151); put("auth", current?.apiKey != null)
        }
    }

    private fun stopServer() {
        server?.let { runCatching { it.stopMyServer() } }
        server = null
    }

    /** Stops accepting browser requests before the engine drains. */
    @Synchronized override fun close() {
        if (worker.isShutdown) return
        worker.execute { retry?.cancel(false); retry = null; stopServer(); status = state("disabled") }
        worker.shutdown()
        check(worker.awaitTermination(30, TimeUnit.SECONDS)) { "Browser integration did not stop" }
    }
}

/** Opens the Qt frontend for a browser request or notification click when none is
 * connected. Rate-limited, detached, no stdin; the launcher itself refuses a second
 * frontend for the profile. The service's own inherited activation token is stale by
 * definition, so a child receives only a token handed over for this launch.
 */
class FrontendLauncher(private val command: List<String>?) {
    @Volatile private var lastSeen = 0L
    @Volatile private var lastActive = false
    @Volatile private var lastLaunch = 0L
    /** A frontend polled; [active] = its window is focused. */
    fun seen(active: Boolean = false) { lastSeen = System.currentTimeMillis(); lastActive = active }
    val connected get() = System.currentTimeMillis() - lastSeen < 3000
    /** The user is looking at the window (focus), as the original background tracker reports. */
    val active get() = connected && lastActive

    /** [explicit]: a user click (notification), allowed sooner than automatic launches. */
    @Synchronized fun request(token: String? = null, explicit: Boolean = false) {
        val now = System.currentTimeMillis()
        if (command == null || connected || now - lastLaunch < (if (explicit) 2000 else 15000)) return
        lastLaunch = now
        runCatching {
            ProcessBuilder(command).apply {
                environment().remove("XDG_ACTIVATION_TOKEN"); environment().remove("DESKTOP_STARTUP_ID")
                token?.let { environment()["XDG_ACTIVATION_TOKEN"] = it }
            }.redirectInput(ProcessBuilder.Redirect.from(File("/dev/null")))
                .redirectOutput(ProcessBuilder.Redirect.DISCARD).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        }
    }

    companion object {
        /** argv as a JSON string array, provided by the explicit runtime launcher only. */
        fun fromEnvironment(value: String?): FrontendLauncher = FrontendLauncher(value?.let {
            runCatching {
                Json.parseToJsonElement(it).jsonArray.map { e -> e.jsonPrimitive.also { p -> require(p.isString) }.content }
            }.getOrNull()?.takeIf { argv -> argv.size in 1..16 && argv.all { a -> a.isNotEmpty() && '\u0000' !in a } && File(argv[0]).isAbsolute }
        })
    }
}
