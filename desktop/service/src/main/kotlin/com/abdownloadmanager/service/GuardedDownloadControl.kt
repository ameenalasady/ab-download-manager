package com.abdownloadmanager.service

import ir.amirab.downloader.DownloadManagerMinimalControl
import ir.amirab.downloader.downloaditem.DownloadItemContext
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Queue schedulers can run while a command suspends for storage I/O. Checking
 * inactive state only at command entry is insufficient: keep their start/stop
 * transitions outside the original engine's move/reset/edit/removal operations.
 * Transfers themselves are not serialized, only job transitions and mutations.
 */
internal class GuardedDownloadControl(private val engine: DownloadManagerMinimalControl) : DownloadManagerMinimalControl by engine {
    private val gate = Mutex()
    suspend fun <T> mutate(block: suspend () -> T): T = gate.withLock { block() }
    override suspend fun startJob(id: Long, context: DownloadItemContext) = mutate { engine.startJob(id, context) }
    override suspend fun stopJob(id: Long, context: DownloadItemContext) = mutate { engine.stopJob(id, context) }
}
