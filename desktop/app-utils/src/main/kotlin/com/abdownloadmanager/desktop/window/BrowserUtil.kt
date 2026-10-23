package com.abdownloadmanager.desktop.window

import com.abdownloadmanager.shared.util.BrowserType
import ir.amirab.util.execAndForget
import ir.amirab.util.platform.Platform
import ir.amirab.util.platform.asDesktop
import ir.amirab.util.toUpUntil
import java.io.File

/**
 * Distributions put browsers in very different places, and a flatpak or snap install is not in
 * `/usr/bin` at all, so every plausible name is looked up in every directory that could hold it
 * instead of hard coding full paths.
 *
 * Flatpak exports are named after the application id, which is why those are in the name lists.
 */
private fun linuxExecutables(vararg names: String): List<File> {
    val home = System.getProperty("user.home")
    val directories = buildList {
        System.getenv("PATH")
            ?.split(File.pathSeparatorChar)
            ?.filter { it.isNotBlank() }
            ?.forEach { add(File(it)) }
        add(File("/usr/bin"))
        add(File("/usr/local/bin"))
        add(File("/bin"))
        add(File("/snap/bin"))
        add(File("/var/lib/flatpak/exports/bin"))
        if (home != null) {
            add(File(home, ".local/share/flatpak/exports/bin"))
            add(File(home, ".local/bin"))
            add(File(home, ".nix-profile/bin"))
        }
    }.distinct()
    return names.flatMap { name ->
        directories.map { it.resolve(name) }
    }
}

abstract class Browser {
    abstract fun getPossibleExecutablePaths(): List<File>

    fun isInstalled(): Boolean {
        return getExecutablePath() != null
    }

    open fun openLink(url: String): Boolean {
        val executablePath = getExecutablePath()
        if (executablePath == null) {
            return false
        }
        val cmd = when (Platform.asDesktop()) {
            Platform.Desktop.Linux -> arrayOf(executablePath.path, url)
            Platform.Desktop.Windows -> arrayOf(executablePath.path, url)
            Platform.Desktop.MacOS -> {
                val appFolder = executablePath.toUpUntil {
                    // in macOS app folders are like /Applications/App Name.app/
                    it.name.endsWith(".app")
                }?.path
                if (appFolder == null) {
                    return false
                }
                arrayOf("open", "-a", appFolder, url)
            }
        }
        return execAndForget(cmd)
    }

    fun getExecutablePath(): File? {
        return getPossibleExecutablePaths().firstOrNull { it.exists() }
    }

    companion object {
        fun getBrowserByType(type: BrowserType): Browser? {
            return when (type) {
                BrowserType.Chrome -> ChromeBrowser
                BrowserType.Firefox -> FirefoxBrowser
                BrowserType.Edge -> EdgeBrowser
                BrowserType.Opera -> OperaBrowser
            }
        }
    }
}

object FirefoxBrowser : Browser() {
    override fun getPossibleExecutablePaths(): List<File> {
        return when (Platform.asDesktop()) {
            Platform.Desktop.Windows -> {
                val firefoxExe = "Mozilla Firefox\\firefox.exe"
                buildList {
                    listOf(
                        "ProgramW6432",
                        "ProgramFiles",
                        "ProgramFiles(x86)",
                        "LOCALAPPDATA",
                    ).forEach {
                        System.getenv(it)?.let { path ->
                            add(File(path, firefoxExe))
                        }
                    }
                }
            }

            Platform.Desktop.MacOS -> {
                listOf(
                    File("/Applications/Firefox.app"),
                )
            }

            Platform.Desktop.Linux -> {
                linuxExecutables(
                    "firefox",
                    "firefox-bin",
                    "firefox-esr",
                    "firefox-developer-edition",
                    "org.mozilla.firefox",
                )
            }
        }
    }
}

object ChromeBrowser : Browser() {
    override fun getPossibleExecutablePaths(): List<File> {
        return when (Platform.asDesktop()) {
            Platform.Desktop.Windows -> {
                val chromeExe = "Google\\Chrome\\Application\\chrome.exe"
                buildList {
                    listOf(
                        "ProgramW6432",
                        "PROGRAMFILES",
                        "PROGRAMFILES(X86)",
                        "LOCALAPPDATA",
                    ).forEach {
                        System.getenv(it)?.let { path ->
                            add(File(path, chromeExe))
                        }
                    }
                }
            }

            Platform.Desktop.MacOS -> {
                listOf(
                    File("/Applications/Google Chrome.app")
                )
            }

            Platform.Desktop.Linux -> {
                linuxExecutables(
                    "google-chrome",
                    "google-chrome-stable",
                    "chromium",
                    "chromium-browser",
                    "com.google.Chrome",
                    "org.chromium.Chromium",
                )
            }
        }
    }
}

object EdgeBrowser : Browser() {
    override fun getPossibleExecutablePaths(): List<File> {
        return when (Platform.asDesktop()) {
            Platform.Desktop.Linux -> {
                linuxExecutables(
                    "microsoft-edge",
                    "microsoft-edge-stable",
                    "com.microsoft.Edge",
                )
            }

            Platform.Desktop.MacOS -> {
                listOf(
                    File("/Applications/Microsoft Edge.app"),
                )
            }

            Platform.Desktop.Windows -> {
                val child = "Microsoft\\Edge\\Application\\msedge.exe"
                buildList {
                    listOf(
                        "ProgramW6432",
                        "PROGRAMFILES",
                        "PROGRAMFILES(X86)",
                        "LOCALAPPDATA",
                    ).forEach {
                        System.getenv(it)?.let { path ->
                            add(File(path, child))
                        }
                    }
                }
            }
        }
    }
}

object OperaBrowser : Browser() {
    override fun getPossibleExecutablePaths(): List<File> {
        return when (Platform.asDesktop()) {
            Platform.Desktop.Windows -> {
                val relativeExe = "Opera\\launcher.exe"
                buildList {
                    System.getenv("LOCALAPPDATA")?.let { path ->
                        add(File(path, "Programs\\$relativeExe"))
                    }
                    listOf(
                        "ProgramW6432",
                        "PROGRAMFILES",
                        "PROGRAMFILES(X86)",
                    ).forEach {
                        System.getenv(it)?.let { path ->
                            add(File(path, relativeExe))
                        }
                    }
                }
            }

            Platform.Desktop.MacOS -> {
                listOf(
                    File("/Applications/Opera.app"),
                )
            }

            Platform.Desktop.Linux -> {
                linuxExecutables(
                    "opera",
                    "opera-stable",
                    "com.opera.Opera",
                )
            }

        }

    }

}

