package ir.amirab.util.desktop.screen

import java.awt.Toolkit
import java.io.File

/**
 * The pieces of the linux desktop configuration that awt does not hand to us.
 *
 * Everything here is best effort: a file that is missing, unreadable or written in a shape we
 * do not recognise simply means "we do not know", and the caller moves on to the next source.
 */
internal object LinuxDesktopConfig {
    /** both XSETTINGS and gtk store dpi values multiplied by this */
    private const val DPI_FIXED_POINT = 1024f

    fun configDir(): File? {
        System.getenv("XDG_CONFIG_HOME")
            ?.takeIf { it.isNotBlank() }
            ?.let { return File(it) }
        return System.getProperty("user.home")
            ?.let { File(it).resolve(".config") }
    }

    fun kdeGlobals(): File? = configDir()?.resolve("kdeglobals")

    /**
     * GNOME and KDE both publish their settings over XSETTINGS, and awt exposes those as
     * desktop properties under a `gnome.` prefix no matter which desktop is actually running.
     */
    private fun xSetting(name: String): Any? {
        return runCatching {
            Toolkit.getDefaultToolkit().getDesktopProperty("gnome.$name")
        }.getOrNull()
    }

    fun xSettingString(name: String): String? = xSetting(name) as? String

    /** @return the dpi XSETTINGS reports for text, or `null` when the desktop does not say */
    fun xSettingsTextDpi(): Float? {
        return (xSetting("Xft/DPI") as? Int)?.let { it / DPI_FIXED_POINT }
    }

    /** @return the dpi the gtk configuration asks for text */
    fun gtkTextDpi(): Float? {
        return gtkSetting("gtk-xft-dpi")?.toFloatOrNull()?.let { it / DPI_FIXED_POINT }
    }

    fun gtkSetting(key: String): String? {
        val configDir = configDir() ?: return null
        for (gtkVersion in listOf("gtk-4.0", "gtk-3.0")) {
            readIniValue(
                file = configDir.resolve(gtkVersion).resolve("settings.ini"),
                section = "Settings",
                key = key,
            )?.let { return it }
        }
        return null
    }

    /** @return the dpi an `Xft.dpi` line in the x resources asks for */
    fun xResourcesTextDpi(): Float? {
        val home = System.getProperty("user.home")?.let(::File) ?: return null
        for (fileName in listOf(".Xresources", ".Xdefaults")) {
            val file = home.resolve(fileName)
            if (!file.isFile) continue
            val dpi = runCatching {
                file.useLines { lines ->
                    lines.map { it.trim() }
                        .filterNot { it.isEmpty() || it.startsWith("!") }
                        .firstOrNull { it.substringBefore(':').trim().equals("Xft.dpi", ignoreCase = true) }
                        ?.substringAfter(':')
                        ?.trim()
                }
            }.getOrNull()?.toFloatOrNull() ?: continue
            return dpi
        }
        return null
    }

    fun readIniValue(file: File, section: String, key: String): String? {
        if (!file.isFile) return null
        return runCatching {
            file.useLines { lines ->
                var inSection = false
                for (rawLine in lines) {
                    val line = rawLine.trim()
                    if (line.isEmpty() || line.startsWith("#") || line.startsWith(";")) continue
                    if (line.startsWith("[") && line.endsWith("]")) {
                        inSection = line.substring(1, line.length - 1).equals(section, ignoreCase = true)
                        continue
                    }
                    if (!inSection) continue
                    val separator = line.indexOf('=')
                    if (separator <= 0) continue
                    if (!line.substring(0, separator).trim().equals(key, ignoreCase = true)) continue
                    return@useLines line.substring(separator + 1).trim().takeIf { it.isNotEmpty() }
                }
                null
            }
        }.getOrNull()
    }
}

/** the dpi every toolkit treats as an unscaled desktop */
internal const val BASE_DPI = 96f

/** accepts both `1.5` and the `150%` form the jvm understands */
internal fun String.toScale(): Float? {
    val value = trim().takeIf { it.isNotEmpty() } ?: return null
    return if (value.endsWith("%")) {
        value.dropLast(1).trim().toFloatOrNull()?.div(100f)
    } else {
        value.toFloatOrNull()
    }
}
