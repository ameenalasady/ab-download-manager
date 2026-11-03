package com.abdownloadmanager.service

import java.nio.file.ClosedWatchServiceException
import java.nio.file.FileSystems
import java.nio.file.Path
import java.nio.file.StandardWatchEventKinds.ENTRY_DELETE
import java.nio.file.StandardWatchEventKinds.OVERFLOW
import java.nio.file.WatchKey

/** Watches download folders (not recursively) and calls [onDeleted] when an entry in one is
 * deleted or moved away, or when events were lost. It only signals: the caller re-checks
 * the files itself, so a missed or spurious event never removes anything on its own.
 * [watch] is called from one thread; [onDeleted] runs on the watcher's daemon thread.
 */
class FolderDeletionWatcher(private val onDeleted: () -> Unit) : AutoCloseable {
    private val service = FileSystems.getDefault().newWatchService()
    private val keys = HashMap<Path, WatchKey>()
    private val thread = Thread({
        try {
            while (true) {
                val key = service.take()
                val signal = key.pollEvents().any { it.kind() == ENTRY_DELETE || it.kind() == OVERFLOW }
                // An invalid key means the folder itself went away; the caller's check covers it.
                if (!key.reset() || signal) onDeleted()
            }
        } catch (_: ClosedWatchServiceException) {
        } catch (_: InterruptedException) {
        }
    }, "deleted-file-watcher").apply { isDaemon = true; start() }

    /** Watch exactly [folders]. Folders that cannot be watched (missing, unreadable, inotify
     * limit reached) are skipped; the caller's periodic check still covers them. */
    @Synchronized
    fun watch(folders: Set<Path>) {
        keys.entries.removeIf { (folder, key) ->
            (folder !in folders || !key.isValid).also { stale -> if (stale) key.cancel() }
        }
        for (folder in folders) {
            if (folder in keys) continue
            runCatching { folder.register(service, ENTRY_DELETE) }.onSuccess { keys[folder] = it }
        }
    }

    @Synchronized
    fun watched(): Set<Path> = keys.keys.toSet()

    override fun close() {
        service.close()
        thread.join(5_000)
    }
}
