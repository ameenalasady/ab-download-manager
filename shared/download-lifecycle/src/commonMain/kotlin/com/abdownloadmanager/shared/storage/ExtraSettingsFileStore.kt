package com.abdownloadmanager.shared.storage

import ir.amirab.downloader.db.TransactionalFileSaver
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.withContext
import kotlinx.serialization.KSerializer
import java.io.File
import java.util.concurrent.ConcurrentHashMap

/** Original per-ID JSON persistence shared by both frontends. No background writer.
 * Strict owners reject corrupt/unfinished storage instead of defaulting and overwriting it.
 * Callers must hold their profile lease and drain command/flow owners before releasing it.
 */
internal class ExtraSettingsFileStore<T>(
    private val folder: File,
    private val saver: TransactionalFileSaver,
    private val serializer: KSerializer<T>,
    private val default: (Long) -> T,
    private val idOf: (T) -> Long,
    private val strict: Boolean,
) {
    private data class Committed<T>(val revision: Long, val value: T)
    private class Entry<T>(value: T) {
        val state = MutableStateFlow(Committed(0L, value))
        private var revision = 0L
        fun committed(value: T) = Committed(++revision, value) // Called with this entry's monitor.
        fun publish(value: Committed<T>) {
            state.update { current -> if (value.revision > current.revision) value else current }
        }
    }
    // Synchronous reads are part of the original public contract. The same per-ID
    // monitor serializes reads and transactional writes; readers cannot mistake
    // our own in-flight .tmp for an abandoned transaction. All I/O is synchronous
    // inside the monitor, with no suspension or downstream callback while held.
    private val entries = ConcurrentHashMap<Long, Entry<T>>()
    private fun entry(id: Long) = entries.computeIfAbsent(id) { Entry(readStored(it)) }
    private fun file(id: Long): File {
        require(id >= 0) { "Extra-settings ID must be nonnegative" }
        return folder.resolve("$id.json")
    }
    private fun readStored(id: Long): T {
        val file = file(id)
        if (strict) {
            require(!saver.getBakFile(file).exists()) { "Unfinished extra-settings transaction; explicit recovery required" }
            if (file.exists()) require(file.isFile && file.length() <= 2 * 1024 * 1024) { "Invalid extra-settings file" }
        }
        if (!file.exists()) return default(id)
        val value = saver.readObject(file, serializer)
        if (strict) require(value != null && idOf(value) == id) { "Corrupt or mismatched extra-settings document" }
        return value ?: default(id)
    }
    fun read(id: Long): T = synchronized(entry(id)) { readStored(id) }
    fun validateStored() {
        require(folder.isDirectory) { "Missing extra-settings directory" }
        val files = folder.listFiles() ?: error("Cannot enumerate extra-settings directory")
        files.forEach { file ->
            val id = file.name.removeSuffix(".json").toLongOrNull()
            require(id != null && id >= 0 && file.name == "$id.json" && file.isFile) { "Unexpected extra-settings entry; explicit recovery required" }
            read(id)
        }
    }
    suspend fun write(value: T) = withContext(Dispatchers.IO) {
        val id = idOf(value)
        val file = file(id)
        val entry = entry(id)
        val committed = synchronized(entry) {
            if (strict) readStored(id)
            saver.writeObject(file, value, serializer)
            entry.committed(value)
        }
        // No downstream collector runs under the file monitor. Revision filtering
        // prevents out-of-order publishers from regressing the committed state.
        entry.publish(committed)
        Unit
    }
    suspend fun delete(id: Long) = withContext(Dispatchers.IO) {
        val file = file(id)
        val entry = entry(id)
        val committed = synchronized(entry) {
            if (strict) readStored(id)
            check(!file.exists() || file.delete()) { "Cannot delete extra-settings document" }
            entry.committed(default(id))
        }
        entry.publish(committed)
        Unit
    }
    fun flow(id: Long, initial: Boolean): Flow<T> = flow {
        val state = entry(id).state
        val baseline = state.value.revision
        emitAll(state.filter { initial || it.revision > baseline }.map { it.value })
    }
}
