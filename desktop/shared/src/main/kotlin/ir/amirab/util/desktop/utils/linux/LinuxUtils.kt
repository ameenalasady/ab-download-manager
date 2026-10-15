package ir.amirab.util.desktop.utils.linux

import ir.amirab.util.desktop.DesktopUtils
import ir.amirab.util.desktop.poweraction.PowerAction
import ir.amirab.util.desktop.poweraction.PowerActionLinux
import ir.amirab.util.execAndForget
import kotlin.concurrent.thread

class LinuxUtils : DesktopUtils {
    private val powerActionForLinux = PowerActionLinux()

    override fun openSystemProxySettings() {
        // this runs straight from a click handler, and trying several settings apps in a row
        // takes long enough to freeze the ui while it happens
        thread(isDaemon = true, name = "open-proxy-settings") {
            proxySettingsCommands().any { execAndForget(it) }
        }
    }

    /**
     * There is no such thing as "the" proxy settings on linux, every desktop has its own, so we
     * ask the one we are running on first and then fall back to whatever else may be installed.
     */
    private fun proxySettingsCommands(): List<Array<String>> {
        val desktop = currentDesktop()
        return buildList {
            when {
                "kde" in desktop || "plasma" in desktop -> {
                    // kcmshell exits with an error when it does not know the module, which lets
                    // the next candidate run - plasma 6 renamed both the binary and the module
                    add(arrayOf("kcmshell6", "kcm_proxy"))
                    add(arrayOf("kcmshell5", "proxy"))
                    add(arrayOf("systemsettings", "kcm_proxy"))
                    add(arrayOf("systemsettings5", "proxy"))
                }

                "cinnamon" in desktop -> add(arrayOf("cinnamon-settings", "network"))
                "mate" in desktop -> add(arrayOf("mate-network-properties"))
                "xfce" in desktop -> add(arrayOf("xfce4-settings-manager"))
                "lxqt" in desktop -> add(arrayOf("lxqt-config"))
            }
            // gnome-control-center is what most of the remaining desktops borrow
            add(arrayOf("gnome-control-center", "network"))
        }
    }

    private fun currentDesktop(): String {
        return (System.getenv("XDG_CURRENT_DESKTOP")
            ?: System.getenv("XDG_SESSION_DESKTOP")
            ?: System.getenv("DESKTOP_SESSION")
            ?: "").lowercase()
    }

    override fun powerAction(): PowerAction {
        return powerActionForLinux
    }
}
