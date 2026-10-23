package com.abdownloadmanager.shared.util.ondownloadcompletion

import ir.amirab.downloader.DownloadManagerEvents
import ir.amirab.downloader.DownloadManagerMinimalControl
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.filterIsInstance
import kotlinx.coroutines.launch

class OnDownloadCompletionActionRunner(
    private val downloadManagerMinimalControl: DownloadManagerMinimalControl,
    private val scope: CoroutineScope,
    private val onDownloadCompletionActionProvider: OnDownloadCompletionActionProvider,
) {
    private var job: Job? = null
    // Keep canceled/replaced actions until their finally blocks have drained.
    private val listeners = mutableListOf<Job>()

    /** Starts listening to completion events; call on the lifecycle owner's context. */
    @Synchronized
    fun startListening() {
        stopListening()
        listeners.removeAll { it.isCompleted }
        job = scope.launch(start = CoroutineStart.UNDISPATCHED) {
            downloadManagerMinimalControl.listOfJobsEvents
                .filterIsInstance<DownloadManagerEvents.OnJobCompleted>()
                .collect {
                    val downloadItem = it.downloadItem
                    onDownloadCompletionActionProvider
                        .getOnDownloadCompletionAction(downloadItem)
                        .forEach { completionAction ->
                            runCatching {
                                completionAction.onDownloadCompleted(downloadItem)
                            }.onFailure { e ->
                                if (e is CancellationException) throw e
                                e.printStackTrace()
                            }
                        }
                }
        }.also { listeners.add(it) }
    }

    /** Join even actions canceled by earlier stopListening/startListening calls. */
    suspend fun closeAndJoin() {
        val pending = synchronized(this) { stopListening(); listeners.toList() }
        pending.forEach { it.cancelAndJoin() }
        synchronized(this) { listeners.removeAll { it.isCompleted } }
    }

    @Synchronized
    fun stopListening() {
        job?.cancel()
        job = null
    }
}
