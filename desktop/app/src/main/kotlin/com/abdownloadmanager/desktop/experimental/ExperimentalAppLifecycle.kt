package com.abdownloadmanager.desktop.experimental

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.awt.ComposeWindow
import com.abdownloadmanager.desktop.AppComponent
import com.abdownloadmanager.desktop.utils.AppInfo
import com.abdownloadmanager.desktop.utils.IntegrationPortBroadcaster
import ir.amirab.downloader.NewDownloadItemProps
import ir.amirab.downloader.downloaditem.DownloadStatus
import ir.amirab.downloader.downloaditem.EmptyContext
import ir.amirab.downloader.downloaditem.http.HttpDownloadItem
import ir.amirab.downloader.utils.OnDuplicateStrategy
import ir.amirab.util.desktop.window.AwtWindowBackend
import ir.amirab.util.desktop.window.ExperimentalDesktopProfile
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put
import java.awt.Frame
import java.awt.Toolkit
import java.awt.Window
import java.io.File
import java.net.URI
import java.security.MessageDigest

/** Diagnostics for marked experimental profiles only. Timed actions require --smoke-test. */
@Composable
fun ExperimentalAppLifecycle(component: AppComponent) {
    if (!ExperimentalDesktopProfile.active) return
    LaunchedEffect(component) {
        suspend fun await(condition: () -> Boolean) = withTimeout(25_000) {
            while (!condition()) delay(50)
        }
        fun windows() = Window.getWindows().filterIsInstance<ComposeWindow>().filter { it.isVisible && it.isDisplayable }
        val root = requireNotNull(ExperimentalDesktopProfile.root)
        val smokeTest = java.lang.Boolean.getBoolean("abdm.experimental.smokeTest")
        check(!smokeTest || ExperimentalDesktopProfile.blocksDesktopFeatures) { "Smoke actions require an integration-disabled disposable profile" }
        await { component.isReady() && (component.useSystemTray.value ||
            (ExperimentalDesktopProfile.draws.get() > 0 && windows().isNotEmpty())) }
        val loadedDownloads = component.downloadSystem.getDownloadItemsBy { true }.size
        var downloadedSha256: String? = null
        fun status(stage: String, log: Boolean = true) {
            val record = buildJsonObject {
                put("pid", ProcessHandle.current().pid())
                put("stage", stage)
                put("toolkit", Toolkit.getDefaultToolkit().javaClass.name)
                put("backend", AwtWindowBackend.current.name)
                put("runtime", System.getProperty("java.runtime.version"))
                put("uiSource", Class.forName("androidx.compose.ui.awt.ComposeWindowPanel").protectionDomain.codeSource.location.toString())
                put("dataDir", AppInfo.dataDir.canonicalPath)
                put("ipcPort", File(AppInfo.definedPaths.configDir.toString(), "app.port").readText().trim().toInt())
                put("integrationPort", IntegrationPortBroadcaster.getIntegrationPort())
                put("drawCallbacks", ExperimentalDesktopProfile.draws.get())
                put("windows", windows().size)
                put("loadedDownloads", loadedDownloads)
                put("downloadCount", component.downloadSystem.getAllDownloadIds().size)
                put("desktopIntegration", ExperimentalDesktopProfile.desktopIntegration != null)
                put("autostart", component.appSettings.autoStartOnBoot.value)
                put("tray", component.useSystemTray.value)
                put("updates", component.updaterManager.isUpdateSupported())
                downloadedSha256?.let { put("downloadSha256", it) }
            }
            val temporary = root.resolve(".session-status.tmp")
            temporary.writeText(record.toString() + "\n")
            java.nio.file.Files.move(temporary.toPath(), root.resolve("session-status.json").toPath(),
                java.nio.file.StandardCopyOption.REPLACE_EXISTING, java.nio.file.StandardCopyOption.ATOMIC_MOVE)
            if (log) println("EXPERIMENTAL_APP_${stage.uppercase()} $record")
        }
        status("ready")
        if (!smokeTest) {
            // Profile-local, URL-free diagnostics, also usable after a background/browser cold start.
            while (true) { delay(1000); status("ready", log = false) }
        }
        // Real app components/windows and persistence, not synthetic keyboard/pointer input.
        component.openSettings()
        await { component.showSettingSlot.value.child != null && windows().size >= 2 }
        val home = windows().first()
        home.extendedState = Frame.MAXIMIZED_BOTH
        await { home.extendedState and Frame.MAXIMIZED_BOTH == Frame.MAXIMIZED_BOTH }
        home.extendedState = Frame.NORMAL
        await { home.extendedState and Frame.MAXIMIZED_BOTH == 0 }
        component.closeSettings()
        await { windows().size == 1 }
        component.openEnterNewURLWindow()
        await { component.enterNewURLWindowSlot.value.child != null && windows().size >= 2 }
        component.closeEnterNewURLWindow()
        await { windows().size == 1 }
        System.getProperty("abdm.experimental.smokeDownloadUrl")?.let { url ->
            val uri = URI(url)
            check(uri.scheme == "http" && uri.host == "127.0.0.1" && uri.path.startsWith("/abdm-native-smoke/"))
            val expected = requireNotNull(System.getProperty("abdm.experimental.smokeDownloadSha256"))
            val system = component.downloadSystem
            val id = system.addDownload(
                newDownload = NewDownloadItemProps(
                    downloadItem = HttpDownloadItem(id = -1, link = url,
                        folder = root.resolve("home/Downloads").path, name = "full-app-payload.bin"),
                    extraConfig = null, onDuplicateStrategy = OnDuplicateStrategy.AddNumbered, context = EmptyContext,
                ), queueId = null, categoryId = null,
            )
            withTimeout(30_000) {
                coroutineScope {
                    val completed = async { system.downloadMonitor.waitForDownloadToFinishOrCancel(id) }
                    system.manualResume(id, EmptyContext)
                    completed.await()
                }
            }
            val item = requireNotNull(system.getDownloadItemById(id))
            check(item.status == DownloadStatus.Completed)
            val file = system.getDownloadFile(item)
            check(file.canonicalFile.parentFile == root.resolve("home/Downloads").canonicalFile)
            downloadedSha256 = withContext(Dispatchers.IO) {
                MessageDigest.getInstance("SHA-256").digest(file.readBytes()).joinToString("") { "%02x".format(it) }
            }
            check(downloadedSha256 == expected) { "Real engine download checksum mismatch" }
            println("EXPERIMENTAL_ENGINE_DOWNLOAD_COMPLETE id=$id sha256=$downloadedSha256")
        }
        // Verify policy boundaries still hold even if preferences are changed in this profile.
        component.appSettings.apiEnabled.value = true
        component.appSettings.autoStartOnBoot.value = true
        component.appSettings.useSystemTray.value = true
        delay(1000) // let debounced settings side effects run; not a window-management workaround
        check(!component.useSystemTray.value && !component.updaterManager.isUpdateSupported())
        check(IntegrationPortBroadcaster.getIntegrationPort() == IntegrationPortBroadcaster.INTEGRATION_DISABLED)
        check(AppInfo.dataDir.canonicalFile == root.resolve("app-data").canonicalFile)
        component.appSettings.apiEnabled.value = false
        component.appSettings.autoStartOnBoot.value = false
        component.appSettings.useSystemTray.value = false
        com.abdownloadmanager.desktop.nativemessaging.NativeMessaging.getDefault(kotlinx.serialization.json.Json).apply {
            installManifests()
            uninstallManifests()
        }
        com.abdownloadmanager.desktop.utils.DesktopEntryCreator.createLinuxDesktopEntry()
        status("smoke_complete")
        component.requestExitApp() // normal engine stop + settings flush, no production IPC
    }
}
