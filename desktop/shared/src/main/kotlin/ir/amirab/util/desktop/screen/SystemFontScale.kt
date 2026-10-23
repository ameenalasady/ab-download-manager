package ir.amirab.util.desktop.screen

import ir.amirab.util.logger.appLogger
import ir.amirab.util.platform.Platform
import ir.amirab.util.platform.isLinux
import kotlin.math.abs

/**
 * How much bigger or smaller the user wants text to be, compared to the desktop they run.
 *
 * Linux desktops keep the text size apart from the display scale, so a user who bumps the
 * interface font from 10pt to 12pt expects text to grow while windows, icons and paddings
 * stay where they are. AWT never tells us about any of that, so we read it ourselves and feed
 * it to compose as the density's font scale, which is exactly the knob that only affects `sp`.
 *
 * Only the interface font size is followed. Everything else a desktop can do to text - a forced
 * font dpi in particular - it also does to windows, and [SystemUiScale] already picks that up, so
 * reading it here as well would apply it twice.
 *
 * A desktop that was left at its defaults gives `1f`, so the app looks unchanged there.
 *
 * Set `abdm.systemFontScale` (or `ABDM_SYSTEM_FONT_SCALE`) to a number to force a value,
 * or to `off` to ignore the desktop and keep the app's own text sizes.
 */
object SystemFontScale {
    /** beyond this the app's fixed row heights start clipping text, so we stop following */
    private val ACCEPTABLE_SCALE = 0.75f..1.75f

    /** below this the difference is not worth re-rasterizing every glyph for */
    private const val TOLERANCE = 0.03f

    private const val OVERRIDE_PROPERTY = "abdm.systemFontScale"
    private const val OVERRIDE_ENV = "ABDM_SYSTEM_FONT_SCALE"

    /**
     * The point size each desktop ships with. Following the deviation from *these* rather than
     * from an absolute size keeps the app as compact as it was designed to be, while still
     * reacting when the user changes the system font.
     */
    private const val KDE_DEFAULT_POINT_SIZE = 10f
    private const val GNOME_DEFAULT_POINT_SIZE = 11f
    private const val FALLBACK_DEFAULT_POINT_SIZE = 10f

    /** multiply the compose density's font scale by this */
    val scale: Float by lazy { detect() }

    private fun detect(): Float {
        readOverride()?.let { return it }
        if (!Platform.isLinux()) return 1f
        val scale = fromSystemFontSize()
            ?.takeIf { it.isFinite() && it in ACCEPTABLE_SCALE }
            ?: return 1f
        if (abs(scale - 1f) <= TOLERANCE) return 1f
        appLogger.i { "following the desktop text size, scaling text by $scale" }
        return scale
    }

    private fun readOverride(): Float? {
        val raw = (System.getProperty(OVERRIDE_PROPERTY) ?: System.getenv(OVERRIDE_ENV))
            ?.trim()
            ?.takeIf { it.isNotEmpty() }
            ?: return null
        if (raw.equals("off", ignoreCase = true) || raw.equals("false", ignoreCase = true)) {
            return 1f
        }
        if (raw.equals("auto", ignoreCase = true)) return null
        return raw.toFloatOrNull()?.takeIf { it.isFinite() && it in ACCEPTABLE_SCALE }
    }

    /**
     * The interface font of the desktop, relative to the size that desktop ships with.
     */
    private fun fromSystemFontSize(): Float? {
        val (pointSize, defaultPointSize) = kdeFontSize()
            ?: gtkFontSize()
            ?: return null
        if (pointSize <= 0f) return null
        return pointSize / defaultPointSize
    }

    /** `font=Noto Sans,10,-1,5,50,0,0,0,0,0`, the point size is the second field */
    private fun kdeFontSize(): Pair<Float, Float>? {
        if (!isKde()) return null
        val kdeGlobals = LinuxDesktopConfig.kdeGlobals() ?: return null
        val pointSize = LinuxDesktopConfig.readIniValue(kdeGlobals, section = "General", key = "font")
            ?.split(',')
            ?.getOrNull(1)
            ?.trim()
            ?.toFloatOrNull()
            ?: return null
        return pointSize to KDE_DEFAULT_POINT_SIZE
    }

    /** `Cantarell 11` / `Noto Sans Bold 10`, the size is the trailing number */
    private fun gtkFontSize(): Pair<Float, Float>? {
        val fontName = LinuxDesktopConfig.xSettingString("Gtk/FontName")
            ?: LinuxDesktopConfig.gtkSetting("gtk-font-name")
            ?: return null
        val pointSize = fontName.trim()
            .substringAfterLast(' ', missingDelimiterValue = "")
            .toFloatOrNull()
            ?: return null
        val default = if (isGnome()) GNOME_DEFAULT_POINT_SIZE else FALLBACK_DEFAULT_POINT_SIZE
        return pointSize to default
    }

    private fun isKde() = currentDesktop().let { "kde" in it || "plasma" in it }

    private fun isGnome() = currentDesktop().let { "gnome" in it || "unity" in it || "cinnamon" in it }

    private fun currentDesktop(): String {
        return (System.getenv("XDG_CURRENT_DESKTOP")
            ?: System.getenv("XDG_SESSION_DESKTOP")
            ?: System.getenv("DESKTOP_SESSION")
            ?: "").lowercase()
    }
}
