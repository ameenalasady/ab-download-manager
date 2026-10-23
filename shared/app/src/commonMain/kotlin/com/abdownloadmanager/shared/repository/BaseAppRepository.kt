package com.abdownloadmanager.shared.repository

import com.abdownloadmanager.shared.IApplicationBackgroundTracker
import com.abdownloadmanager.shared.storage.SpeedLimitMode.*
import com.abdownloadmanager.shared.storage.appsettings.BaseAppSettingsStorage
import com.abdownloadmanager.shared.storage.SupportedSizeUnits
import com.abdownloadmanager.shared.util.AutoStartManager
import com.abdownloadmanager.shared.util.SizeAndSpeedUnitProvider
import com.abdownloadmanager.shared.util.DownloadSystem
import com.abdownloadmanager.shared.util.DownloadEnginePolicy
import com.abdownloadmanager.shared.util.DownloadEnginePolicyController
import com.abdownloadmanager.shared.util.DownloadEnginePolicyBinding
import com.abdownloadmanager.shared.util.autoremove.RemovedDownloadsFromDiskTracker
import com.abdownloadmanager.shared.util.category.CategoryManager
import com.abdownloadmanager.shared.util.proxy.ProxyManager
import com.abdownloadmanager.shared.util.systemusage.SystemUsageMonitor
import ir.amirab.downloader.DownloadManager
import ir.amirab.downloader.DownloadSettings
import ir.amirab.downloader.monitor.IDownloadMonitor
import ir.amirab.util.datasize.ConvertSizeConfig
import ir.amirab.util.flow.mapStateFlow
import ir.amirab.util.flow.withPrevious
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.debounce
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.flow.stateIn
import kotlin.time.Duration.Companion.milliseconds

open class BaseAppRepository(
    protected val scope: CoroutineScope,
    protected val appSettings: BaseAppSettingsStorage,
    protected val proxyManager: ProxyManager,
    protected val downloadSystem: DownloadSystem,
    protected val downloadSettings: DownloadSettings,
    protected val removedDownloadsFromDiskTracker: RemovedDownloadsFromDiskTracker,
    protected val categoryManager: CategoryManager,
    private val appUsageMonitor: SystemUsageMonitor,
    private val backgroundTracker: IApplicationBackgroundTracker,
) : SizeAndSpeedUnitProvider {
    val theme = appSettings.theme
    val uiScale = appSettings.uiScale
    private val downloadManager: DownloadManager = downloadSystem.downloadManager
    private val downloadMonitor: IDownloadMonitor = downloadSystem.downloadMonitor

    val maxConcurrentDownloads = appSettings.maxConcurrentDownloads
    val speedLimitMode = appSettings.useSpeedLimit
    val speedLimiter = appSettings.speedLimit

    private val shouldUseSpeedLimit = speedLimitMode.flatMapLatest { mode ->
        when (mode) {
            Enabled -> flowOf(true)
            Disabled -> flowOf(false)
            EnabledWhenBessy -> combine(
                appUsageMonitor.isUserInteractingWithSystemFlow,
                backgroundTracker.isInBackgroundFlow
            ) { isUserInteracting, isInBackground ->
                isUserInteracting && isInBackground
            }
        }
    }.stateIn(scope, SharingStarted.Eagerly, speedLimitMode.value == Enabled)
    val isSpeedLimitApplied = MutableStateFlow(false)

    val threadCount = appSettings.threadCount
    val minPartSize = appSettings.minPartSize
    val dynamicPartCreation = appSettings.dynamicPartCreation
    val useServerLastModifiedTime = appSettings.useServerLastModifiedTime
    val appendExtensionToIncompleteDownloads = appSettings.appendExtensionToIncompleteDownloads
    val useSparseFileAllocation = appSettings.useSparseFileAllocation
    val maxDownloadRetryCount = appSettings.maxDownloadRetryCount
    val useAverageSpeed = appSettings.useAverageSpeed
    val saveLocation = appSettings.defaultDownloadFolder
    val apiEnabled = appSettings.apiEnabled
    val apiPort = appSettings.apiPort
    val apiAuthEnabled = appSettings.apiAuthEnabled
    val apiAuthKey = appSettings.apiAuthKey
    val trackDeletedFilesOnDisk = appSettings.trackDeletedFilesOnDisk

    override val sizeUnit = appSettings.sizeUnit.mapStateFlow {
        it.toConfig()
    }
    override val speedUnit = appSettings.speedUnit.mapStateFlow {
        it.toConfig()
    }


    fun setSizeUnit(sizeUnit: ConvertSizeConfig) {
        SupportedSizeUnits.fromConfig(sizeUnit)?.let {
            appSettings.sizeUnit.value = it
        }
    }

    fun setSpeedUnit(speedUnit: ConvertSizeConfig) {
        SupportedSizeUnits.fromConfig(speedUnit)?.let {
            appSettings.speedUnit.value = it
        }
    }

    private val enginePolicyController = DownloadEnginePolicyController(
        downloadSettings, downloadManager, downloadSystem.manualDownloadQueue, downloadMonitor,
    )
    private val enginePolicyBinding: DownloadEnginePolicyBinding

    fun boot() {
        val policy = currentEnginePolicy()
        enginePolicyController.apply(policy)
        isSpeedLimitApplied.value = policy.globalSpeedLimit > 0
    }

    /** Only the extracted engine subscription; not a full platform/repository shutdown. */
    suspend fun closeEnginePolicyAndJoin() = enginePolicyBinding.closeAndJoin()

    private fun currentEnginePolicy() = DownloadEnginePolicy(
        threadCount = threadCount.value,
        minPartSize = minPartSize.value,
        dynamicPartCreation = dynamicPartCreation.value,
        useServerLastModifiedTime = useServerLastModifiedTime.value,
        appendExtensionToIncompleteDownloads = appendExtensionToIncompleteDownloads.value,
        useSparseFileAllocation = useSparseFileAllocation.value,
        maxDownloadRetryCount = maxDownloadRetryCount.value,
        globalSpeedLimit = DownloadEnginePolicy.effectiveSpeedLimit(shouldUseSpeedLimit.value, speedLimiter.value),
        maxConcurrentDownloads = maxConcurrentDownloads.value,
        useAverageSpeed = useAverageSpeed.value,
    )

    init {
        saveLocation
            .debounce(500.milliseconds)
            .withPrevious()
            .onEach { (oldDownloadFolder, newDownloadFolder) ->
                if (oldDownloadFolder == null) {
                    return@onEach
                }
                categoryManager.updateCategoryFoldersBasedOnDefaultDownloadFolder(
                    previousDownloadFolder = oldDownloadFolder,
                    currentDownloadFolder = newDownloadFolder,
                )
            }.launchIn(scope)
        //maybe its better to move this to another place
        appSettings.autoStartOnBoot
            .debounce(500.milliseconds)
            .onEach { enabled ->
                AutoStartManager.startOnBoot(enabled)
            }.launchIn(scope)
        val engineInputs: List<Flow<Any>> = listOf(
            shouldUseSpeedLimit, speedLimiter, useAverageSpeed, threadCount, minPartSize,
            dynamicPartCreation, useServerLastModifiedTime, appendExtensionToIncompleteDownloads,
            useSparseFileAllocation, maxDownloadRetryCount, maxConcurrentDownloads,
        )
        enginePolicyBinding = DownloadEnginePolicyBinding(
            scope, combine(engineInputs) { currentEnginePolicy() }, enginePolicyController::apply,
        ) { isSpeedLimitApplied.value = it.globalSpeedLimit > 0 }
        trackDeletedFilesOnDisk
            .debounce(500.milliseconds)
            .onEach { enabled ->
                if (enabled) {
                    removedDownloadsFromDiskTracker.removeDownloadsThatFilesAreMissing()
                    removedDownloadsFromDiskTracker.start()
                } else {
                    removedDownloadsFromDiskTracker.stop()
                }
            }.launchIn(scope)
    }
}
