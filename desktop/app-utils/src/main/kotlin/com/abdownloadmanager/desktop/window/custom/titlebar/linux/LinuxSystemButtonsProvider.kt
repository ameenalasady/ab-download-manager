package com.abdownloadmanager.desktop.window.custom.titlebar.linux

import com.abdownloadmanager.desktop.window.custom.titlebar.SystemButtonPositionProvider
import com.abdownloadmanager.desktop.window.custom.titlebar.SystemButtonType
import com.abdownloadmanager.desktop.window.custom.titlebar.SystemButtonsPosition
import java.io.File
import java.util.concurrent.TimeUnit

object LinuxSystemButtonsProvider : SystemButtonPositionProvider {
    override fun getPositions(): SystemButtonsPosition? {
        return runCatching {
            getSystemButtonLayout()
        }.getOrNull()
    }
}

private fun getSystemButtonLayout(): SystemButtonsPosition? {
    val desktop = currentDesktop()

    return when {
        "gnome" in desktop || "unity" in desktop -> parseColonLayout(
            runCommand(
                "gsettings",
                "get",
                "org.gnome.desktop.wm.preferences",
                "button-layout"
            )
        )

        "cinnamon" in desktop -> parseColonLayout(
            runCommand(
                "gsettings",
                "get",
                "org.cinnamon.desktop.wm.preferences",
                "button-layout"
            )
        )

        "mate" in desktop -> parseColonLayout(
            runCommand(
                "gsettings",
                "get",
                "org.mate.Marco.general",
                "button-layout"
            )
        )

        "xfce" in desktop -> parsePipeLayout(
            runCommand(
                "xfconf-query",
                "-c",
                "xfwm4",
                "-p",
                "/general/button_layout"
            )
        )

        "kde" in desktop || "plasma" in desktop -> parseKDELayout(
            File(System.getenv("XDG_CONFIG_HOME") ?: File(System.getProperty("user.home"), ".config").path, "kwinrc")
        )

        else -> null
    }?.takeIf { it.buttons.isNotEmpty() }
}

private fun currentDesktop(): String {
    return (System.getenv("XDG_CURRENT_DESKTOP")
        ?: System.getenv("XDG_SESSION_DESKTOP")
        ?: System.getenv("DESKTOP_SESSION")
        ?: "unknown").lowercase()
}

// For GNOME / MATE → colon-separated layout
private fun parseColonLayout(layoutRaw: String?): SystemButtonsPosition? {
    val layout = layoutRaw?.removeSurrounding("'", "'")?.trim() ?: return null
    val (left, right) = layout.split(":", limit = 2).map { it.trim() + "," }.let {
        parseButtons(it.getOrNull(0).orEmpty()) to parseButtons(it.getOrNull(1).orEmpty())
    }

    return pick(left, right)
}

// For XFCE → pipe-separated layout
private fun parsePipeLayout(layout: String?): SystemButtonsPosition? {
    val parts = layout?.split("|")?.map { it.trim() } ?: return null
    return pick(
        left = parseButtons(parts.getOrNull(0).orEmpty()),
        right = parseButtons(parts.getOrNull(1).orEmpty()),
    )
}

/**
 * For KDE → parse kwinrc.
 *
 * The buttons live in the decoration section and are written as single letters rather than
 * names, `I` for minimize, `A` for maximize, `X` for close, and a handful of others we have no
 * button for. A stock plasma has `ButtonsOnLeft=MS` and `ButtonsOnRight=HIAX`, so both sides
 * have to be read before deciding which one holds the buttons we can actually draw.
 */
private fun parseKDELayout(file: File): SystemButtonsPosition? {
    if (!file.exists()) return null
    val decoration = readIniSection(file, sectionSuffix = "kdecoration") ?: return null
    return pick(
        left = parseKDEButtons(decoration["ButtonsOnLeft"]),
        right = parseKDEButtons(decoration["ButtonsOnRight"]),
    )
}

private fun pick(
    left: List<SystemButtonType>,
    right: List<SystemButtonType>,
): SystemButtonsPosition? {
    return when {
        // the side that carries the close button is the side the user looks at
        left.contains(SystemButtonType.Close) -> SystemButtonsPosition(left, isLeft = true)
        right.isNotEmpty() -> SystemButtonsPosition(right, isLeft = false)
        left.isNotEmpty() -> SystemButtonsPosition(left, isLeft = true)
        else -> null
    }
}

/**
 * The section is looked up by suffix because plasma keeps renaming it between releases
 * (`org.kde.kdecoration2`, `org.kde.kdecoration3`, ...).
 */
private fun readIniSection(file: File, sectionSuffix: String): Map<String, String>? {
    return runCatching {
        val values = mutableMapOf<String, String>()
        var inSection = false
        file.forEachLine { rawLine ->
            val line = rawLine.trim()
            if (line.isEmpty() || line.startsWith("#")) return@forEachLine
            if (line.startsWith("[") && line.endsWith("]")) {
                inSection = line.trim('[', ']').lowercase().substringAfterLast('.').matches(Regex("${sectionSuffix.lowercase()}\\d*"))
                return@forEachLine
            }
            if (!inSection) return@forEachLine
            val separator = line.indexOf('=')
            if (separator <= 0) return@forEachLine
            values[line.substring(0, separator).trim()] = line.substring(separator + 1).trim()
        }
        values.takeIf { it.isNotEmpty() }
    }.getOrNull()
}

private fun parseKDEButtons(raw: String?): List<SystemButtonType> {
    return raw.orEmpty().mapNotNull {
        when (it) {
            'I' -> SystemButtonType.Minimize
            'A' -> SystemButtonType.Maximize
            'X' -> SystemButtonType.Close
            // M menu, S on all desktops, H help, F keep above, B keep below, L shade,
            // none of which we have a button for
            else -> null
        }
    }
}

private fun parseButtons(raw: String): List<SystemButtonType> {
    return raw.split(",", "|", " ")
        .mapNotNull {
            when (it.lowercase()) {
                "close" -> SystemButtonType.Close
                "maximize" -> SystemButtonType.Maximize
                "minimize" -> SystemButtonType.Minimize
                else -> null
            }
        }
}

private fun runCommand(vararg args: String): String? {
    return try {
        val process = ProcessBuilder(*args).redirectErrorStream(true).start()
        // These settings commands have tiny output. Wait before reading so a hung command
        // cannot block readText forever and bypass the timeout.
        if (!process.waitFor(2, TimeUnit.SECONDS)) {
            process.destroyForcibly()
            process.inputStream.close()
            return null
        }
        process.inputStream.reader().use { it.readText().trim() }
            .takeIf { process.exitValue() == 0 }
    } catch (_: Exception) {
        null
    }
}
