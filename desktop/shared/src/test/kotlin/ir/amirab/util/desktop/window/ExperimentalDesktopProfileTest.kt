package ir.amirab.util.desktop.window

import ir.amirab.util.ExternalAppEnvironment
import java.io.File
import java.nio.file.Files
import kotlin.test.*

class ExperimentalDesktopProfileTest {
    @Test fun ordinaryLaunchDoesNotSelectExperimentalProfile() {
        assertNull(ExperimentalDesktopProfile.validateRoot(null, "/normal/home", emptyMap(), "/normal/tmp"))
    }

    @Test fun markedProfileRequiresAllHomeAndXdgPathsToMatch() = withProfile { root, env ->
        assertEquals(root, validate(root.path, root.resolve("home").path, env))
        for (key in env.keys) {
            assertFailsWith<IllegalArgumentException> {
                validate(root.path, root.resolve("home").path, env + (key to "/normal/home"))
            }
        }
        assertFailsWith<IllegalArgumentException> {
            validate(root.path, "/normal/home", env)
        }
    }

    @Test fun javaTemporaryDirectoryMustAlsoBeIsolated() = withProfile { root, env ->
        assertFailsWith<IllegalArgumentException> {
            ExperimentalDesktopProfile.validateRoot(root.path, root.resolve("home").path, env, "/tmp")
        }
    }

    @Test fun missingOrForeignMarkerIsRejected() = withProfile { root, env ->
        root.resolve(ExperimentalDesktopProfile.MARKER).writeText("unrelated")
        assertFailsWith<IllegalArgumentException> {
            validate(root.path, root.resolve("home").path, env)
        }
        root.resolve(ExperimentalDesktopProfile.MARKER).delete()
        assertFailsWith<IllegalArgumentException> {
            validate(root.path, root.resolve("home").path, env)
        }
    }

    @Test fun symlinkedDataDirectoryIsRejected() = withProfile { root, env ->
        root.resolve("app-data").delete()
        Files.createSymbolicLink(root.resolve("app-data").toPath(), root.resolve("data").toPath())
        assertFailsWith<IllegalArgumentException> {
            validate(root.path, root.resolve("home").path, env)
        }
    }

    @Test fun nestedDataSymlinksAreRejectedAtTheKotlinBoundaryToo() = withProfile { root, env ->
        root.resolve("app-data/config").mkdir()
        Files.createSymbolicLink(root.resolve("app-data/config/link").toPath(), root.resolve("data").toPath())
        assertFailsWith<IllegalArgumentException> {
            validate(root.path, root.resolve("home").path, env)
        }
    }

    @Test fun relativeAndNonNormalizedRootsAreRejected() = withProfile { root, env ->
        for (raw in listOf("relative", root.resolve("tmp/..").path)) {
            assertFailsWith<IllegalArgumentException> {
                validate(raw, root.resolve("home").path, env)
            }
        }
    }

    @Test fun desktopFeaturesStayDisabledWithoutAnExplicitActivation() = withProfile { root, _ ->
        assertNull(ExperimentalDesktopProfile.validateDesktopIntegration(root))
    }

    @Test fun activatedIntegrationUsesOnlyDedicatedNativeExecutables() = withProfile { root, _ ->
        val home = root.parentFile
        listOf("ABDownloadManagerNativeWayland", "ABDownloadManagerNativeWaylandCli", "ABDownloadManagerNativeWaylandHost").forEach {
            home.resolve(".local/bin/$it").apply { parentFile.mkdirs(); writeText("#!/bin/sh\nexit 0\n"); setExecutable(true) }
        }
        root.resolve(ExperimentalDesktopProfile.INTEGRATION_MARKER).writeText(
            "kind=${ExperimentalDesktopProfile.INTEGRATION_KIND}\nhome=${home.path}\n")
        val policy = assertNotNull(ExperimentalDesktopProfile.validateDesktopIntegration(root))
        assertEquals(home, policy.home)
        assertEquals("ABDownloadManagerNativeWaylandCli", policy.cliExecutable.name)
        assertEquals("ABDownloadManagerNativeWaylandHost", policy.hostExecutable.name)
        policy.hostExecutable.delete()
        assertFailsWith<IllegalArgumentException> { ExperimentalDesktopProfile.validateDesktopIntegration(root) }
    }

    @Test fun foreignAndSymlinkedActivationMarkersAreRejected() = withProfile { root, _ ->
        val marker = root.resolve(ExperimentalDesktopProfile.INTEGRATION_MARKER)
        marker.writeText("kind=foreign\nhome=${root.parentFile.path}\n")
        assertFailsWith<IllegalArgumentException> { ExperimentalDesktopProfile.validateDesktopIntegration(root) }
        marker.delete()
        val target = root.resolve("tmp/marker").apply { writeText("kind=foreign\n") }
        Files.createSymbolicLink(marker.toPath(), target.toPath())
        assertFailsWith<IllegalArgumentException> { ExperimentalDesktopProfile.validateDesktopIntegration(root) }
    }

    @Test fun externalAppsUseTheOriginalDesktopContextNotTheProfileHome() = withProfile { root, _ ->
        val policy = ExperimentalDesktopProfile.DesktopIntegration(root.parentFile)
        val prefix = ExternalAppEnvironment.PROPERTY_PREFIX
        val properties = mapOf(prefix + "enabled" to "true", prefix + "HOME" to policy.home.path,
            prefix + "XDG_DATA_HOME" to policy.home.resolve("custom-data").path, prefix + "DISPLAY" to ":9")
        val environment = ExperimentalDesktopProfile.desktopEnvironment(policy, properties)
        assertEquals(policy.home.path, environment["HOME"])
        assertEquals(policy.home.resolve("custom-data").path, environment["XDG_DATA_HOME"])
        assertEquals(":9", environment["DISPLAY"])
        assertTrue(environment.containsKey("XDG_CONFIG_HOME"))
        assertNull(environment["XDG_CONFIG_HOME"], "Unset XDG paths must be removed, not left isolated")
        assertNull(environment[ExternalAppEnvironment.CONTEXT_VARIABLE])
    }

    @Test fun missingOrForeignDesktopContextIsRejected() = withProfile { root, _ ->
        val policy = ExperimentalDesktopProfile.DesktopIntegration(root.parentFile)
        val prefix = ExternalAppEnvironment.PROPERTY_PREFIX
        for (properties in listOf(emptyMap(), mapOf(prefix + "enabled" to "true", prefix + "HOME" to root.resolve("home").path))) {
            assertFailsWith<IllegalArgumentException> { ExperimentalDesktopProfile.desktopEnvironment(policy, properties) }
        }
    }

    private fun validate(raw: String, userHome: String, env: Map<String, String>): File? =
        ExperimentalDesktopProfile.validateRoot(raw, userHome, env, env["TMPDIR"])

    private fun withProfile(block: (File, Map<String, String>) -> Unit) {
        val home = Files.createTempDirectory("abdm-experiment-test").toFile().canonicalFile
        val root = home.resolve("profile").apply { mkdir() }
        try {
            root.resolve(ExperimentalDesktopProfile.MARKER).writeText(ExperimentalDesktopProfile.MARKER_CONTENT)
            listOf("home", "config", "data", "cache", "tmp", "app-data").forEach { root.resolve(it).mkdir() }
            val env = mapOf("HOME" to "home", "XDG_CONFIG_HOME" to "config", "XDG_DATA_HOME" to "data",
                "XDG_CACHE_HOME" to "cache", "TMPDIR" to "tmp").mapValues { root.resolve(it.value).path }
            block(root, env)
        } finally {
            // Files.walk does NOT follow the deliberately introduced symlinks.
            Files.walk(home.toPath()).use { paths ->
                paths.sorted(Comparator.reverseOrder()).forEach { Files.deleteIfExists(it) }
            }
        }
    }
}
