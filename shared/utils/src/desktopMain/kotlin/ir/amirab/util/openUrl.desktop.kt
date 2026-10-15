package ir.amirab.util

import ir.amirab.util.platform.Platform
import ir.amirab.util.platform.isLinux
import java.awt.Desktop
import java.net.URI
import kotlin.concurrent.thread

actual object URLOpener {
    actual fun openUrl(url: String) {
        // this is called straight from click handlers, and both the awt call and the browser it
        // starts take long enough on linux to freeze the ui while it happens
        thread(isDaemon = true, name = "open-url") {
            if (openWithDesktop(url)) return@thread
            openWithXdgOpen(url)
        }
    }

    private fun openWithDesktop(url: String): Boolean {
        return runCatching {
            val desktop = Desktop.getDesktop()
            if (!desktop.isSupported(Desktop.Action.BROWSE)) return false
            desktop.browse(URI(url))
            true
        }.getOrElse { false }
    }

    /**
     * AWT reaches the browser through libgio, which is not part of every environment we can be
     * installed into - a flatpak, a minimal desktop or a session without the gnome libraries
     * leaves us with `xdg-open`, which knows how to find the browser on its own.
     */
    private fun openWithXdgOpen(url: String): Boolean {
        if (!Platform.isLinux()) return false
        return execAndForget(arrayOf("xdg-open", url))
    }
}
