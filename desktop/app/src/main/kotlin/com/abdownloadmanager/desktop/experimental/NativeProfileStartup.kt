package com.abdownloadmanager.desktop.experimental

import com.abdownloadmanager.desktop.utils.AppInfo
import ir.amirab.util.desktop.window.ExperimentalDesktopProfile
import ir.amirab.util.startup.AbstractDesktopStartupManager
import java.nio.file.Files
import java.nio.file.StandardCopyOption

/** Only the explicitly activated profile may manage the real user's native autostart. */
class NativeProfileStartup(private val profile: ExperimentalDesktopProfile.DesktopIntegration) :
    AbstractDesktopStartupManager(AppInfo.displayName, profile.mainExecutable.path, listOf("--background")) {
    private val marker = "X-ABDM-Native-Wayland=true"
    private fun checkOwnership() {
        val file = profile.autostartFile
        require(file.canonicalFile == file.absoluteFile && !Files.isSymbolicLink(file.toPath()))
        require(!file.exists() || marker in file.readText()) { "Refusing an unrelated autostart file; activate through the installer" }
    }

    override fun install() {
        checkOwnership()
        val file = profile.autostartFile
        file.parentFile.mkdirs()
        val content = buildString {
            appendLine("[Desktop Entry]")
            appendLine("Type=Application")
            appendLine("Name=${AppInfo.displayName}")
            appendLine("Exec=${getExecutableWithArgs()}")
            appendLine("Icon=${profile.home.resolve(".local/opt/ab-download-manager-native-wayland/ABDownloadManager.png")}")
            appendLine("Terminal=false")
            appendLine("NoDisplay=true")
            appendLine("StartupNotify=false")
            appendLine("X-GNOME-Autostart-enabled=true")
            appendLine("X-KDE-autostart-phase=2")
            appendLine(marker)
        }
        val temporary = Files.createTempFile(file.parentFile.toPath(), ".abdm-autostart-", ".tmp")
        try {
            temporary.toFile().writeText(content)
            Files.move(temporary, file.toPath(), StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE)
        } finally {
            Files.deleteIfExists(temporary)
        }
    }

    override fun uninstall() {
        checkOwnership()
        profile.autostartFile.delete()
    }
}
