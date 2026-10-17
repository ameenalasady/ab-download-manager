package ir.amirab.util.desktop.window

import java.awt.Toolkit

/**
 * The backend of this JVM's actual AWT toolkit, not the desktop session type.
 *
 * Both DISPLAY and WAYLAND_DISPLAY normally exist on Plasma. Neither those variables nor
 * awt.toolkit.name prove which toolkit was initialized. In particular a Wayland native
 * handle must never be passed to Xlib, even if a separate XOpenDisplay would succeed.
 */
enum class AwtWindowBackend {
    X11,
    Wayland,
    Other;

    companion object {
        /** AWT chooses one toolkit for the lifetime of the process. Unknown fails closed. */
        val current: AwtWindowBackend by lazy {
            fromToolkitClassName(runCatching { Toolkit.getDefaultToolkit().javaClass.name }.getOrNull())
        }

        internal fun fromToolkitClassName(className: String?): AwtWindowBackend = when (className) {
            "sun.awt.X11.XToolkit" -> X11
            "sun.awt.wl.WLToolkit" -> Wayland
            else -> Other
        }
    }
}
