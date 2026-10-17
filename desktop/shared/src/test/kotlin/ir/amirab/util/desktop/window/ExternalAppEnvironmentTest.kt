package ir.amirab.util.desktop.window

import ir.amirab.util.ExternalAppEnvironment
import kotlin.test.*

class ExternalAppEnvironmentTest {
    @AfterTest fun reset() = ExternalAppEnvironment.configure(emptyMap())

    @Test fun ordinaryLaunchLeavesTheChildEnvironmentUntouched() {
        ExternalAppEnvironment.configure(emptyMap())
        val process = ProcessBuilder("not-executed")
        val before = process.environment().toMap()
        assertSame(process, ExternalAppEnvironment.applyTo(process))
        assertEquals(before, process.environment())
        assertFalse(ExternalAppEnvironment.configured)
    }

    @Test fun onlyExternalChildrenReceiveOverridesAndUnsetVariablesAreRemoved() {
        val originalHome = System.getenv("HOME")
        val userHome = System.getProperty("user.home")
        val overrides = mutableMapOf<String, String?>("HOME" to "/desktop/home", "XDG_DATA_HOME" to null,
            "XDG_CACHE_HOME" to "", "DISPLAY" to ":9", ExternalAppEnvironment.CONTEXT_VARIABLE to null)
        ExternalAppEnvironment.configure(overrides)
        overrides["HOME"] = "/later/mutation"
        val external = ProcessBuilder("not-executed").apply {
            environment()["HOME"] = "/private/profile/home"
            environment()["XDG_DATA_HOME"] = "/private/profile/data"
            environment()[ExternalAppEnvironment.CONTEXT_VARIABLE] = "internal context"
        }
        ExternalAppEnvironment.applyTo(external)
        assertEquals("/desktop/home", external.environment()["HOME"])
        assertFalse(external.environment().containsKey("XDG_DATA_HOME"))
        assertFalse(external.environment().containsKey(ExternalAppEnvironment.CONTEXT_VARIABLE))
        assertEquals("", external.environment()["XDG_CACHE_HOME"])
        assertEquals(":9", external.environment()["DISPLAY"])
        assertEquals(originalHome, System.getenv("HOME"))
        assertEquals(userHome, System.getProperty("user.home"))
        assertEquals(originalHome, ProcessBuilder("internal-child-not-executed").environment()["HOME"])
    }

    @Test fun unrelatedEnvironmentOverridesAreNotAccepted() {
        assertFailsWith<IllegalArgumentException> { ExternalAppEnvironment.configure(mapOf("LD_PRELOAD" to "injected")) }
    }
}
