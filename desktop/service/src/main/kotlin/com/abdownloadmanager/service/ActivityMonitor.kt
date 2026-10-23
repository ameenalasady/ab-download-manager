package com.abdownloadmanager.service

import kotlinx.serialization.json.*
import java.io.File
import kotlin.concurrent.thread

/** Real inputs of the original activity-dependent ("EnabledWhenBessy") speed limit.
 *
 * Interaction: Plasma on Wayland refuses org.freedesktop.ScreenSaver.GetSessionIdleTime and
 * has no Mutter IdleMonitor, so the original D-Bus tiers report nothing here. Instead a
 * read-only helper subscribes to the compositor's
 * ext-idle-notify-v1 v2 input-only idle notification with the original 30 s threshold and prints
 * `ready`, `idle` or `active`. Ready does not imply recent user input. Background: no frontend window has focus (the original
 * DesktopApplicationBackgroundTracker), as reported by the frontend's snapshot polls.
 *
 * Like the original's WhileSubscribed monitor, the helper runs only while [wanted]. When it
 * is missing, exits or never reports, [available] is false and the original fallback
 * applies: "not interacting", so no limit.
 */
class MonitoredSystemActivity(
    private val command: List<String>?,
    private val launcher: FrontendLauncher,
    private val onChange: () -> Unit,
) : SystemActivity, AutoCloseable {
    @Volatile private var ready = false
    @Volatile private var interacting = false
    @Volatile private var process: Process? = null
    private var restarts = ArrayDeque<Long>()
    @Volatile private var closed = false

    override val available get() = ready
    override val selectable get() = command != null
    override fun isUserInteracting() = ready && interacting
    override fun isFrontendInBackground() = !launcher.active

    /** Starts or stops the helper; idempotent. */
    @Synchronized fun wanted(value: Boolean) {
        if (closed || command == null) return
        if (value && process?.isAlive != true) start() else if (!value) stopHelper()
    }

    @Synchronized private fun start() {
        val now = System.currentTimeMillis()
        while (restarts.isNotEmpty() && now - restarts.first() > 60_000) restarts.removeFirst()
        if (restarts.size >= 5) return // Crash loop: stay on the original fallback.
        restarts.addLast(now)
        val started = runCatching {
            ProcessBuilder(command!!).redirectError(ProcessBuilder.Redirect.DISCARD).start()
        }.getOrNull() ?: return
        process = started
        thread(isDaemon = true, name = "activity-monitor") {
            runCatching {
                started.inputStream.bufferedReader().useLines { lines ->
                    for (line in lines) {
                        if (line.length > 32) break
                        val changed = synchronized(this) {
                            if (closed || process !== started) return@thread
                            val before = isUserInteracting()
                            when (line.trim()) {
                                "ready" -> { ready = true; interacting = false }
                                "idle" -> interacting = false
                                "active" -> interacting = true
                            }
                            before != isUserInteracting()
                        }
                        if (changed) onChange()
                    }
                }
            }
            // A protocol error or EOF must not leave a connected read-only helper orphaned.
            if (started.isAlive) started.destroy()
            synchronized(this) { if (process === started) { process = null; val was = ready; ready = false; if (was) onChange() } }
        }
    }

    @Synchronized private fun stopHelper() {
        val current = process ?: return
        process = null
        // A read-only helper, not a writer: closing stdin ends it, TERM is the fallback.
        runCatching { current.outputStream.close() }
        if (!current.waitFor(2, java.util.concurrent.TimeUnit.SECONDS)) {
            current.destroy() // Read-only helper only; never a writer or an arbitrary PID.
            if (!current.waitFor(3, java.util.concurrent.TimeUnit.SECONDS)) {
                process = current // Retain ownership on failure; never SIGKILL or pretend it joined.
                error("Read-only activity helper did not stop")
            }
        }
        val was = ready; ready = false; interacting = false
        if (was) onChange()
    }

    fun snapshot() = buildJsonObject {
        put("available", ready); put("interacting", isUserInteracting()); put("background", isFrontendInBackground())
        put("monitor", if (command == null) "unavailable" else if (process?.isAlive == true) (if (ready) "running" else "starting") else "stopped")
    }

    @Synchronized override fun close() { closed = true; stopHelper() }

    companion object {
        /** argv as a JSON string array, provided by the explicit runtime launcher only. */
        fun commandFrom(value: String?): List<String>? = value?.let {
            runCatching {
                Json.parseToJsonElement(it).jsonArray.map { e -> e.jsonPrimitive.also { p -> require(p.isString) }.content }
            }.getOrNull()?.takeIf { argv -> argv.size in 1..16 && argv.all { a -> a.isNotEmpty() && '\u0000' !in a } && File(argv[0]).isAbsolute }
        }
    }
}
