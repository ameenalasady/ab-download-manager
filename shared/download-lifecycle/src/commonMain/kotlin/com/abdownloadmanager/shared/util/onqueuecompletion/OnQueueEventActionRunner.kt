package com.abdownloadmanager.shared.util.onqueuecompletion

import ir.amirab.downloader.queue.QueueEvent
import ir.amirab.downloader.queue.QueueManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.launch
import kotlinx.coroutines.flow.collect

class OnQueueEventActionRunner(
    private val queueManager: QueueManager,
    private val scope: CoroutineScope,
    private val onQueueCompletionActionProvider: OnQueueCompletionActionProvider,
) {
    private var job: Job? = null
    private val listeners = mutableListOf<Job>()

    /**
     * Starts listening to queue events and executes the corresponding actions.
     */
    @Synchronized
    fun startListening() {
        stopListening()
        listeners.removeAll { it.isCompleted }
        job = scope.launch(start = CoroutineStart.UNDISPATCHED) {
            queueManager.queueEvents.collect {
                when (it) {
                    is QueueEvent.OnQueueBecomesEmpty -> {
                        val actions = onQueueCompletionActionProvider.getOnQueueEventActions(it.queueId)
                        actions.forEach { action ->
                            action.onQueueCompleted(it.queueId)
                        }
                    }

                    is QueueEvent.QueueEndTimeReached -> {
                        if (!it.wasActive) {
                            return@collect
                        }
                        val actions = onQueueCompletionActionProvider.getOnQueueEventActions(it.queueId)
                        actions.forEach { action ->
                            action.onQueueEndTimeReached(it.queueId)
                        }
                    }

                    is QueueEvent.OnQueueStartTimeReached -> {
                        // nothing
                    }
                }
            }
        }.also { listeners.add(it) }
    }

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


