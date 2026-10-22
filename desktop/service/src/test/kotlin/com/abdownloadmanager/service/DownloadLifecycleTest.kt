package com.abdownloadmanager.service

import com.abdownloadmanager.shared.util.downloaderror.*
import com.abdownloadmanager.shared.util.DownloadEnginePolicy
import com.abdownloadmanager.shared.util.DownloadEnginePolicyBinding
import kotlinx.coroutines.flow.MutableStateFlow
import com.abdownloadmanager.shared.util.downloaderror.faileddownloads.*
import com.abdownloadmanager.shared.util.ondownloadcompletion.*
import ir.amirab.downloader.*
import ir.amirab.downloader.downloaditem.*
import ir.amirab.downloader.downloaditem.http.HttpDownloadItem
import ir.amirab.downloader.exception.TooManyErrorException
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlin.test.*

class DownloadLifecycleTest {
    private class Events : DownloadManagerMinimalControl {
        override val listOfJobsEvents = MutableSharedFlow<DownloadManagerEvents>()
        override suspend fun startJob(id: Long, context: DownloadItemContext) = Unit
        override suspend fun stopJob(id: Long, context: DownloadItemContext) = Unit
        override fun canActivateJob(id: Long) = true
    }
    private val item = HttpDownloadItem(link = "http://127.0.0.1:12345/fail", id = 9007199254740997L,
        folder = "/fixture", name = "failure.bin")

    @Test fun sharedEnginePolicyBootAndJoinedDebounce() = runBlocking {
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val policy = DownloadEnginePolicy(threadCount = 4, minPartSize = 1048576,
            dynamicPartCreation = false, useServerLastModifiedTime = true,
            appendExtensionToIncompleteDownloads = true, useSparseFileAllocation = false,
            maxDownloadRetryCount = 7, globalSpeedLimit = 12345, maxConcurrentDownloads = 2, useAverageSpeed = false)
        val settings = DownloadSettings()
        policy.applyTo(settings)
        assertEquals(4, settings.defaultThreadCount)
        assertEquals(1048576L, settings.minPartSize)
        assertFalse(settings.dynamicPartCreationMode); assertTrue(settings.useServerLastModifiedTime)
        assertTrue(settings.appendExtensionToIncompleteDownloads); assertFalse(settings.useSparseFileAllocation)
        assertEquals(7, settings.maxDownloadRetryCount); assertEquals(12345L, settings.globalSpeedLimit)
        assertEquals(0L, DownloadEnginePolicy.effectiveSpeedLimit(false, 123))
        assertEquals(1L, DownloadEnginePolicy.effectiveSpeedLimit(true, 0))
        assertEquals(Long.MAX_VALUE, DownloadEnginePolicy.effectiveSpeedLimit(true, Long.MAX_VALUE))
        val source = MutableStateFlow(policy)
        val applied = mutableListOf<DownloadEnginePolicy>()
        val binding = DownloadEnginePolicyBinding(scope, source, { applied.add(it); it.applyTo(settings) })
        source.value = policy.copy(threadCount = 6)
        source.value = policy.copy(threadCount = 8, globalSpeedLimit = 0)
        withTimeout(3000) { while (applied.isEmpty()) delay(10) }
        assertEquals(listOf(source.value), applied)
        assertEquals(8, settings.defaultThreadCount); assertEquals(0L, settings.globalSpeedLimit)
        source.value = policy.copy(threadCount = 10)
        binding.closeAndJoin(); binding.closeAndJoin()
        source.value = policy.copy(threadCount = 12)
        delay(650)
        assertEquals(1, applied.size)
        assertEquals(8, settings.defaultThreadCount)
        scope.cancel()
    }

    @Test fun originalFailurePolicyClearsOnRetryAndJoins() = runBlocking {
        val events = Events()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        val storage = FailedDownloadErrorStorageInMemory()
        val registry = DownloadErrorMapperRegistry(listOf(serviceFallbackErrorMapper()))
        val failures = FailedDownloads(events, storage, registry, scope)
        failures.boot(); failures.boot()
        assertEquals(1, events.listOfJobsEvents.subscriptionCount.value)
        assertNull(registry.getReason(CancellationException("pause")))
        events.listOfJobsEvents.emit(DownloadManagerEvents.OnJobCanceled(item, EmptyContext,
            TooManyErrorException(IllegalStateException("<b>fixture failure</b>"))))
        val reason = storage.reasons.value.getValue(item.id)
        assertEquals("Something Went Wrong", reason.title)
        assertTrue(reason.description.contains("IllegalStateException"))
        assertTrue(reason.description.contains("<b>fixture failure</b>"))
        assertEquals("java.lang.IllegalStateException", reason.throwableName)
        events.listOfJobsEvents.emit(DownloadManagerEvents.OnJobStarting(item, EmptyContext))
        assertTrue(storage.reasons.value.isEmpty())
        failures.closeAndJoin(); failures.closeAndJoin()
        assertEquals(0, events.listOfJobsEvents.subscriptionCount.value)
        events.listOfJobsEvents.emit(DownloadManagerEvents.OnJobCanceled(item, EmptyContext, Exception("late")))
        assertTrue(storage.reasons.value.isEmpty())
        scope.cancel()
    }

    @Test fun completionIsExactlyEventDrivenAndShutdownJoinsActionFinally() = runBlocking {
        val events = Events()
        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Unconfined)
        var calls = 0
        var finalized = false
        val entered = CompletableDeferred<Unit>()
        val runner = OnDownloadCompletionActionRunner(events, scope, object : OnDownloadCompletionActionProvider {
            override suspend fun getOnDownloadCompletionAction(downloadItem: IDownloadItem) = listOf(object : OnDownloadCompletionAction {
                override suspend fun onDownloadCompleted(downloadItem: IDownloadItem) {
                    calls++
                    try { entered.complete(Unit); awaitCancellation() }
                    finally { withContext(NonCancellable) { delay(80); finalized = true } }
                }
            })
        })
        runner.startListening()
        assertEquals(1, events.listOfJobsEvents.subscriptionCount.value)
        events.listOfJobsEvents.emit(DownloadManagerEvents.OnJobStarting(item, EmptyContext))
        assertEquals(0, calls)
        events.listOfJobsEvents.emit(DownloadManagerEvents.OnJobCompleted(item, EmptyContext))
        entered.await()
        runner.stopListening() // A non-joined legacy stop must not lose the action reference.
        runner.startListening() // Nor may replacing the subscription lose its finalizer.
        runner.closeAndJoin(); runner.closeAndJoin()
        assertTrue(finalized)
        assertEquals(0, events.listOfJobsEvents.subscriptionCount.value)
        events.listOfJobsEvents.emit(DownloadManagerEvents.OnJobCompleted(item, EmptyContext))
        assertEquals(1, calls)
        assertTrue(NoOpOnDownloadCompletionActionProvider().getOnDownloadCompletionAction(item).isEmpty())
        scope.cancel()
    }
}
