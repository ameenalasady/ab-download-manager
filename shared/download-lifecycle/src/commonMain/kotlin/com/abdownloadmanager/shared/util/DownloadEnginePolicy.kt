package com.abdownloadmanager.shared.util

import ir.amirab.downloader.DownloadManager
import ir.amirab.downloader.DownloadSettings
import ir.amirab.downloader.monitor.IDownloadMonitor
import ir.amirab.downloader.queue.ManualDownloadQueue
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.*
import kotlin.time.Duration.Companion.milliseconds

/** Immutable engine-policy snapshot. Frontends still own preferences/platform observations.
 * No file format, UI toolkit, second scheduler or implicit platform side effects here.
 */
data class DownloadEnginePolicy(
    val threadCount: Int,
    val minPartSize: Long,
    val dynamicPartCreation: Boolean,
    val useServerLastModifiedTime: Boolean,
    val appendExtensionToIncompleteDownloads: Boolean,
    val useSparseFileAllocation: Boolean,
    val maxDownloadRetryCount: Int,
    val globalSpeedLimit: Long,
    val maxConcurrentDownloads: Int,
    val useAverageSpeed: Boolean,
) {
    /** Must also run before manager.boot(): incomplete-extension selection is boot-only. */
    fun applyTo(settings: DownloadSettings) {
        settings.defaultThreadCount = threadCount
        settings.minPartSize = minPartSize
        settings.dynamicPartCreationMode = dynamicPartCreation
        settings.useServerLastModifiedTime = useServerLastModifiedTime
        settings.appendExtensionToIncompleteDownloads = appendExtensionToIncompleteDownloads
        settings.useSparseFileAllocation = useSparseFileAllocation
        settings.maxDownloadRetryCount = maxDownloadRetryCount
        settings.globalSpeedLimit = globalSpeedLimit
    }

    companion object {
        /** Original BaseAppRepository policy: zero means unlimited, enabled clamps to >=1. */
        fun effectiveSpeedLimit(enabled: Boolean, bytesPerSecond: Long) =
            if (enabled) bytesPerSecond.coerceAtLeast(1) else 0L
    }
}

/** Owner-context application of the original repository's engine side effects.
 * In particular maxConcurrentDownloads governs MANUAL requests, not named queues.
 */
class DownloadEnginePolicyController(
    private val settings: DownloadSettings,
    private val manager: DownloadManager,
    private val manual: ManualDownloadQueue,
    private val monitor: IDownloadMonitor,
) {
    private var previous: DownloadEnginePolicy? = null

    fun apply(policy: DownloadEnginePolicy) {
        val old = settings.copy()
        policy.applyTo(settings)
        if (previous == null || old.copy(globalSpeedLimit = 0) != settings.copy(globalSpeedLimit = 0)) {
            manager.reloadSetting()
        }
        if (previous?.globalSpeedLimit != policy.globalSpeedLimit) manager.limitGlobalSpeed(policy.globalSpeedLimit)
        if (previous?.maxConcurrentDownloads != policy.maxConcurrentDownloads) manual.setMaxConcurrent(policy.maxConcurrentDownloads)
        if (previous?.useAverageSpeed != policy.useAverageSpeed) monitor.useAverageSpeed = policy.useAverageSpeed
        previous = policy
    }
}

/** Compose's existing debounced policy subscription, now explicitly joinable by owners.
 * Service commands use the controller directly after synchronous persistence instead.
 */
@OptIn(kotlinx.coroutines.FlowPreview::class)
class DownloadEnginePolicyBinding(
    scope: CoroutineScope,
    policies: Flow<DownloadEnginePolicy>,
    applyPolicy: (DownloadEnginePolicy) -> Unit,
    onApplied: (DownloadEnginePolicy) -> Unit = {},
) {
    private val job = policies.distinctUntilChanged().debounce(500.milliseconds)
        .onEach { applyPolicy(it); onApplied(it) }.launchIn(scope)

    suspend fun closeAndJoin() = job.cancelAndJoin()
}
