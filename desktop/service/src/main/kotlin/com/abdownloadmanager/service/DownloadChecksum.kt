package com.abdownloadmanager.service

import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.serialization.json.*
import java.nio.file.Files
import java.nio.file.LinkOption.NOFOLLOW_LINKS
import java.nio.file.Path
import java.nio.file.StandardOpenOption.READ
import java.nio.file.attribute.BasicFileAttributes
import java.security.MessageDigest

/** One bounded, read-only hash worker per service; no automatic work or persisted results. */
internal class DownloadChecksum {
    companion object {
        val algorithms = mapOf("MD5" to 32, "SHA-1" to 40, "SHA-256" to 64, "SHA-512" to 128)
        fun expected(algorithm: String, value: JsonElement): String? {
            require(algorithm in algorithms) { "Unsupported checksum algorithm" }
            if (value == JsonNull) return null
            val hash = (value as? JsonPrimitive)?.takeIf { it.isString }?.content ?: error("Invalid expected checksum")
            require(hash.length == algorithms.getValue(algorithm) && hash.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) { "Expected checksum must be hexadecimal with the algorithm's exact length" }
            return "$algorithm:${hash.lowercase()}"
        }
    }
    private data class State(val id: Long, val algorithm: String, val state: String = "running", val percent: Int = 0, val digest: String = "")
    private val value = MutableStateFlow<State?>(null)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    fun isReading(id: Long) = value.value?.let { it.id == id && it.state == "running" } == true
    fun snapshot(): JsonElement = value.value?.let { s -> buildJsonObject {
        put("id", s.id.toString()); put("algorithm", s.algorithm); put("state", s.state); put("percent", s.percent); put("digest", s.digest)
    } } ?: JsonNull
    fun start(id: Long, path: Path, algorithm: String) {
        require(algorithm in algorithms) { "Unsupported checksum algorithm" }
        check(value.value?.state != "running") { "Cancel the current checksum calculation first" }
        val before = Files.readAttributes(path, BasicFileAttributes::class.java, NOFOLLOW_LINKS)
        require(before.isRegularFile) { "Completed file is missing or not a regular file" }
        value.value = State(id, algorithm)
        job = scope.launch {
            try {
                val digest = MessageDigest.getInstance(algorithm)
                var count = 0L
                Files.newInputStream(path, READ, NOFOLLOW_LINKS).use { input ->
                    val buffer = ByteArray(64 * 1024)
                    while (true) {
                        ensureActive()
                        val size = input.read(buffer)
                        if (size < 0) break
                        count += size; digest.update(buffer, 0, size)
                        val percent = if (before.size() == 0L) 0 else ((count.toDouble() / before.size()) * 100).toInt().coerceIn(0, 99)
                        value.update { it?.copy(percent = percent) }
                    }
                }
                ensureActive()
                val after = Files.readAttributes(path, BasicFileAttributes::class.java, NOFOLLOW_LINKS)
                check(after.isRegularFile && count == before.size() && before.size() == after.size() &&
                    before.lastModifiedTime() == after.lastModifiedTime() && before.fileKey() == after.fileKey()) { "File changed during calculation" }
                val hex = digest.digest().joinToString("") { "%02x".format(it) }
                value.update { it?.copy(state = "done", percent = 100, digest = hex) }
            } catch (e: CancellationException) {
                value.update { it?.copy(state = "cancelled", digest = "") }; throw e
            } catch (_: Exception) {
                // No arbitrary exception/path/credential text in snapshots.
                value.update { it?.copy(state = "error", digest = "") }
            }
        }
    }
    suspend fun cancel(id: Long) {
        require(value.value?.id == id) { "Checksum target changed" }
        job?.cancelAndJoin()
        // Covers cancellation before the worker enters its try block.
        value.update { if (it?.state == "running") it.copy(state = "cancelled") else it }
    }
    fun invalidate(id: Long) {
        check(!isReading(id)) { "Cancel checksum calculation first" }
        value.update { if (it?.id == id) null else it }
    }
    suspend fun close() { scope.coroutineContext.job.cancelAndJoin() }
}
