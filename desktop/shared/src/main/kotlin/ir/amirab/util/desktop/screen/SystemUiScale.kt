package ir.amirab.util.desktop.screen

import ir.amirab.util.logger.appLogger
import ir.amirab.util.platform.Platform
import ir.amirab.util.platform.isLinux
import java.awt.GraphicsEnvironment
import kotlin.math.abs

/**
 * Java has no Wayland toolkit, so we always go through XWayland,
 * and `sun.awt.X11GraphicsDevice` only ever reports **integer** scale factors there.
 * On a KDE Plasma (or GNOME) session that uses fractional scaling - 125%, 150%, 175% -
 * AWT tells us the scale is `1` while every other window on the desktop is scaled up,
 * which makes our windows and everything inside them far too small.
 *
 * We can not change the scale AWT applies, but compose lets us override the density.
 * [correctionFactor] is how far AWT is off, so we can render at the density the desktop
 * expects and ask AWT for windows large enough to hold that content.
 *
 * This deliberately only looks for how much the desktop scales *windows*. Text is a separate
 * setting on every linux desktop and is handled by [SystemFontScale], so that turning up the
 * font size does not also inflate every icon, padding and window in the app.
 *
 * Set `abdm.systemUiScale` (or the `ABDM_SYSTEM_UI_SCALE` environment variable) to a
 * number to force a scale, or to `off` to keep whatever AWT reports.
 */
object SystemUiScale {
    /** anything outside of this is a misconfigured desktop, not a scale we should follow */
    private val ACCEPTABLE_SCALE = 0.5f..8f

    /** a correction this small is not worth the fractional density it introduces */
    private const val TOLERANCE = 0.02f

    private const val OVERRIDE_PROPERTY = "abdm.systemUiScale"
    private const val OVERRIDE_ENV = "ABDM_SYSTEM_UI_SCALE"

    /**
     * multiply the compose density (and any window size we hand over to AWT) by this
     * to match the scale of the desktop we are running on.
     */
    val correctionFactor: Float by lazy { computeCorrectionFactor() }

    /**
     * The scale the desktop applies to windows, as far as we can tell.
     * `null` when we have no idea, which on a healthy desktop means "not scaled".
     */
    private val desktopScale: Float? by lazy { detectDesktopScale() }

    private fun computeCorrectionFactor(): Float {
        val detected = desktopScale ?: return 1f
        val awtScale = awtScale() ?: return 1f
        val factor = detected / awtScale
        if (!factor.isFinite() || factor <= 0f) return 1f
        if (abs(factor - 1f) <= TOLERANCE) return 1f
        appLogger.i {
            "desktop ui scale is $detected but awt reports $awtScale, scaling the ui by $factor"
        }
        return factor
    }

    private fun awtScale(): Float? {
        return runCatching {
            GraphicsEnvironment.getLocalGraphicsEnvironment()
                .defaultScreenDevice
                .defaultConfiguration
                .defaultTransform
                .scaleX
                .toFloat()
        }.getOrNull()?.takeIf { it > 0f && it.isFinite() }
    }

    private fun detectDesktopScale(): Float? {
        val override = readOverride()
        if (override != null) {
            return override.takeIf { it > 0f }?.sanitizeScale()
        }
        // windows and macOS report the desktop scale to AWT correctly
        if (!Platform.isLinux()) return null
        // hidpi was turned off on purpose, we have nothing to correct
        if (System.getProperty("sun.java2d.uiScale.enabled") == "false") return null
        return fromJavaScaleProperty()
            // the dpi the desktop publishes over XSETTINGS is what kwin hands to xwayland
            // clients, which is exactly what we are, so it is the closest to the truth
            ?: textDpiScale()
            ?: fromEnvironment()
            ?: fromKdeGlobals()
    }

    /**
     * `off` means the user does not want us to touch the scale at all,
     * we express that as a scale of `0` so the detection stops here.
     */
    private fun readOverride(): Float? {
        val raw = (System.getProperty(OVERRIDE_PROPERTY) ?: System.getenv(OVERRIDE_ENV))
            ?.trim()
            ?.takeIf { it.isNotEmpty() }
            ?: return null
        if (raw.equals("off", ignoreCase = true) || raw.equals("false", ignoreCase = true)) {
            return 0f
        }
        if (raw.equals("auto", ignoreCase = true)) return null
        return raw.toScale()
    }

    /**
     * A scale was already asked for, either by the user or by skiko, which probes the desktop
     * the same way JBR does. The JVM truncates it to an integer on linux (JDK-8214227),
     * so we still have to make up the difference ourselves.
     *
     * A plain `1` means nothing was found rather than "the desktop runs at 100%",
     * so in that case we keep looking.
     */
    private fun fromJavaScaleProperty(): Float? {
        val raw = System.getProperty("sun.java2d.uiScale")
            ?: System.getenv("J2D_UISCALE")
            ?: return null
        return raw.toScale()
            ?.takeIf { it != 1f }
            ?.sanitizeScale()
    }

    private fun fromEnvironment(): Float? {
        val windowScale = System.getenv("GDK_SCALE")?.toScale()
            ?: System.getenv("QT_SCALE_FACTOR")?.toScale()
        val textScale = System.getenv("GDK_DPI_SCALE")?.toScale()
        if (windowScale == null && textScale == null) return null
        // plenty of distros export `GDK_SCALE=1`, that tells us nothing
        return ((windowScale ?: 1f) * (textScale ?: 1f))
            .takeIf { it != 1f }
            ?.sanitizeScale()
    }

    private fun fromKdeGlobals(): Float? {
        val kdeGlobals = LinuxDesktopConfig.kdeGlobals() ?: return null
        LinuxDesktopConfig.readIniValue(kdeGlobals, section = "KScreen", key = "ScaleFactor")
            ?.toScale()
            ?.sanitizeScale()
            ?.let { return it }
        // looks like `eDP-1=1.5;HDMI-A-1=1.5;`, every screen we can handle is the first one
        return LinuxDesktopConfig.readIniValue(kdeGlobals, section = "KScreen", key = "ScreenScaleFactors")
            ?.split(';')
            ?.firstOrNull { it.contains('=') }
            ?.substringAfter('=')
            ?.toScale()
            ?.sanitizeScale()
    }

    /**
     * The dpi a desktop asks for text with is the same one it scales its windows by - kwin hands
     * xwayland clients the display scale as an `Xft.dpi`, and gtk desktops do the same - so this
     * is a reading of the display scale rather than of anything specific to text.
     */
    private fun textDpiScale(): Float? {
        val dpi = LinuxDesktopConfig.xSettingsTextDpi()
            ?: LinuxDesktopConfig.gtkTextDpi()
            ?: LinuxDesktopConfig.xResourcesTextDpi()
            ?: return null
        return (dpi / BASE_DPI).sanitizeScale()
    }

    private fun Float.sanitizeScale(): Float? {
        return takeIf { it.isFinite() && it in ACCEPTABLE_SCALE }
    }
}
