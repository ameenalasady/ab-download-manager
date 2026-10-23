package com.abdownloadmanager.desktop.window

import java.awt.Window
import ir.amirab.util.platform.Platform
import ir.amirab.util.platform.isLinux
import javax.swing.SwingUtilities

/**
 * Raises this window above the others and gives it the keyboard focus.
 *
 * A bare [Window.toFront] is enough on windows and macOS, but most linux window managers refuse it
 * for a window that is not already the one the user is working in, and only mark it as demanding
 * attention instead - on a kde plasma session that is a blinking task bar entry and nothing else.
 * Briefly claiming the always on top state is the one hint kwin, mutter and xfwm all honour, so we
 * take it, raise the window, and hand it straight back.
 *
 * The caller is responsible for un-minimizing the window first, through the compose window state,
 * so that compose does not put it back where it was.
 */
fun Window.bringToFront() {
    runCatching {
        val claimAlwaysOnTop = Platform.isLinux() && !isAlwaysOnTop && isAlwaysOnTopSupported
        if (claimAlwaysOnTop) {
            isAlwaysOnTop = true
        }
        toFront()
        requestFocus()
        if (claimAlwaysOnTop) {
            // give the window manager the chance to act on the raise before taking the reason
            // for it away again
            SwingUtilities.invokeLater {
                runCatching { isAlwaysOnTop = false }
            }
        }
    }
}
