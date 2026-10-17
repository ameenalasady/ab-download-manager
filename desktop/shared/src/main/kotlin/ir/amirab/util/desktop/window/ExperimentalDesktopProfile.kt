package ir.amirab.util.desktop.window

import ir.amirab.util.ExternalAppEnvironment
import java.io.File
import java.nio.file.Files
import java.util.Properties
import java.util.concurrent.atomic.AtomicLong

/** Explicit application-level isolation, not a security sandbox. Normal launches are unchanged. */
object ExperimentalDesktopProfile {
    const val ROOT_PROPERTY = "abdm.experimental.profileRoot"
    const val MARKER = ".abdm-experimental-profile"
    const val MARKER_CONTENT = "ABDM isolated desktop experiment v1\n"
    const val INTEGRATION_MARKER = ".abdm-desktop-integration.properties"
    const val INTEGRATION_KIND = "abdm-native-desktop-integration-v1"

    val root: File? by lazy {
        validateRoot(System.getProperty(ROOT_PROPERTY), System.getProperty("user.home"), System.getenv(),
            System.getProperty("java.io.tmpdir"))
    }
    val active: Boolean get() = root != null
    val dataDir: File? get() = root?.resolve("app-data")
    val desktopIntegration: DesktopIntegration? by lazy { root?.let(::validateDesktopIntegration) }
    val blocksDesktopFeatures: Boolean get() = active && desktopIntegration == null

    data class DesktopIntegration(val home: File, val testLabel: String? = null) {
        val mainExecutable: File get() = executable("ABDownloadManagerNativeWayland")
        val cliExecutable: File get() = executable("ABDownloadManagerNativeWaylandCli")
        val hostExecutable: File get() = executable("ABDownloadManagerNativeWaylandHost")
        val autostartFile: File get() = home.resolve(".config/autostart/com.abdownloadmanager.desktop")
        private fun executable(name: String) = home.resolve(".local/bin/$name").also {
            require(it.isFile && it.canExecute() && it.canonicalFile == it.absoluteFile) { "Invalid native integration executable: $it" }
        }
    }

    internal fun validateDesktopIntegration(root: File): DesktopIntegration? {
        val marker = root.resolve(INTEGRATION_MARKER)
        if (!marker.exists() && !Files.isSymbolicLink(marker.toPath())) return null
        require(marker.isFile && !Files.isSymbolicLink(marker.toPath())) { "Invalid desktop integration marker" }
        val properties = Properties().apply { marker.reader(Charsets.UTF_8).use { load(it) } }
        require(properties.getProperty("kind") == INTEGRATION_KIND) { "Unknown desktop integration policy" }
        val home = File(requireNotNull(properties.getProperty("home")))
        require(home.isAbsolute && home.canonicalFile == home.absoluteFile && home.absoluteFile == home.absoluteFile.normalize())
        require(root.toPath().startsWith(home.toPath()) && home != root) { "Integration home must contain the profile" }
        val testLabel = properties.getProperty("testLabel")?.also {
            require(it.length <= 64 && it.none(Char::isISOControl)) { "Invalid test label" }
        }
        return DesktopIntegration(home, testLabel).also {
            it.mainExecutable
            it.cliExecutable
            it.hostExecutable
        }
    }
    /** Restore the desktop context for external programs ONLY after validating explicit activation. */
    fun configureExternalApps() {
        val policy = desktopIntegration ?: return
        val prefix = ExternalAppEnvironment.PROPERTY_PREFIX
        val properties = (ExternalAppEnvironment.variables + "enabled").mapNotNull { name ->
            System.getProperty(prefix + name)?.let { prefix + name to it }
        }.toMap()
        ExternalAppEnvironment.configure(desktopEnvironment(policy, properties))
    }

    internal fun desktopEnvironment(policy: DesktopIntegration, properties: Map<String, String>): Map<String, String?> {
        val prefix = ExternalAppEnvironment.PROPERTY_PREFIX
        require(properties[prefix + "enabled"] == "true") { "Missing original desktop environment; use the native launcher" }
        val environment = ExternalAppEnvironment.variables.associateWith { properties[prefix + it] }
        require(environment["HOME"] == policy.home.path) { "Desktop environment does not match the activated integration home" }
        // Do not leak our private relaunch context into VLC/browsers and their descendants.
        return environment + (ExternalAppEnvironment.CONTEXT_VARIABLE to null)
    }

    val draws = AtomicLong()
    val label: String get() {
        val backend = if (System.getProperty("abdm.experimental.backend") == "x11") "XWayland" else "Native Wayland"
        return "$backend EXPERIMENT" + (desktopIntegration?.testLabel?.let { " — $it" } ?: "")
    }

    fun windowTitle(title: String): String = if (!active || title.endsWith(label)) title else "$title — $label"

    /** Called BEFORE AppInfo, its stores, or single-instance discovery are initialized. */
    fun validateBackend() {
        val actual = AwtWindowBackend.current
        if (actual == AwtWindowBackend.Wayland) {
            check(active && java.lang.Boolean.getBoolean("abdm.experimental.nativeWayland")) {
                "Full native Wayland launches require the isolated experimental launcher/profile."
            }
        }
        if (!active) return
        val expected = when (System.getProperty("abdm.experimental.backend")) {
            "wayland" -> AwtWindowBackend.Wayland
            "x11" -> AwtWindowBackend.X11
            else -> error("Experimental backend must be explicitly wayland or x11")
        }
        check(actual == expected) { "Requested $expected but actual AWT backend is $actual; refusing fallback" }
        check((expected == AwtWindowBackend.Wayland) == java.lang.Boolean.getBoolean("abdm.experimental.nativeWayland"))
    }

    internal fun validateRoot(raw: String?, userHome: String?, environment: Map<String, String>, javaTemp: String?): File? {
        if (raw == null) return null
        val root = File(raw)
        require(root.isAbsolute && root.absoluteFile == root.absoluteFile.normalize() && root.canonicalFile == root.absoluteFile) {
            "Experimental profile root must be absolute, normalized and free of symlink aliases"
        }
        require(root.resolve(MARKER).let { !Files.isSymbolicLink(it.toPath()) && it.isFile && it.readText() == MARKER_CONTENT }) {
            "Missing experimental profile ownership marker; use the experimental launcher"
        }
        for (child in listOf("home", "config", "data", "cache", "tmp", "app-data")) {
            val file = root.resolve(child)
            require(file.isDirectory && file.canonicalFile == file.absoluteFile.normalize()) {
                "Invalid or symlinked experimental profile directory: $file"
            }
        }
        val expected = mapOf("HOME" to "home", "XDG_CONFIG_HOME" to "config", "XDG_DATA_HOME" to "data",
            "XDG_CACHE_HOME" to "cache", "TMPDIR" to "tmp")
        require(userHome == root.resolve("home").path) { "user.home is not isolated" }
        require(javaTemp == root.resolve("tmp").path) { "java.io.tmpdir is not isolated" }
        Files.walk(root.resolve("app-data").toPath()).use { paths ->
            require(paths.noneMatch { Files.isSymbolicLink(it) }) { "Symlinks are not supported inside experimental app data" }
        }
        expected.forEach { (key, child) ->
            require(environment[key] == root.resolve(child).path) { "$key is not isolated" }
        }
        return root
    }
}
