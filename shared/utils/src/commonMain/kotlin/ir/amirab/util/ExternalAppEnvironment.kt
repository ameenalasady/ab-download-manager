package ir.amirab.util

/**
 * Environment for user-facing programs, not ABDM's own JVM/IPC children. An experimental
 * profile may isolate ABDM's stores without giving VLC, browsers or file managers that
 * private HOME. Empty by default: ordinary launches retain their existing behavior.
 */
object ExternalAppEnvironment {
    const val PROPERTY_PREFIX = "abdm.experimental.desktopEnvironment."
    const val CONTEXT_VARIABLE = "ABDM_NATIVE_DESKTOP_ENVIRONMENT"
    val variables = setOf(
        "HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_CACHE_HOME", "XDG_STATE_HOME",
        "TMPDIR", "DISPLAY", "SKIKO_RENDER_API", "WAYLAND_DEBUG",
    )

    @Volatile
    private var overrides: Map<String, String?> = emptyMap()

    val configured: Boolean get() = overrides.isNotEmpty()

    fun configure(environment: Map<String, String?>) {
        require(environment.keys.all { it in variables || it == CONTEXT_VARIABLE })
        overrides = environment.toMap()
    }

    fun applyTo(builder: ProcessBuilder): ProcessBuilder = builder.apply {
        val environment = environment()
        overrides.forEach { (name, value) ->
            if (value == null) environment.remove(name) else environment[name] = value
        }
    }
}
