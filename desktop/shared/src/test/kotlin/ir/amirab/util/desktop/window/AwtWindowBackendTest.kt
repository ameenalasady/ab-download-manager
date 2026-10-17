package ir.amirab.util.desktop.window

import ir.amirab.util.desktop.screen.SystemUiScale
import kotlin.test.*

class AwtWindowBackendTest {
    @Test fun recognizesActualToolkitNames() {
        assertEquals(AwtWindowBackend.X11, AwtWindowBackend.fromToolkitClassName("sun.awt.X11.XToolkit"))
        assertEquals(AwtWindowBackend.Wayland, AwtWindowBackend.fromToolkitClassName("sun.awt.wl.WLToolkit"))
    }

    @Test fun unknownAndHeadlessToolkitsFailClosed() {
        for (name in listOf(null, "", "WLToolkit", "XToolkit", "sun.awt.HeadlessToolkit",
            "sun.lwawt.macosx.LWCToolkit", "sun.awt.windows.WToolkit", "example.XToolkit")) {
            assertEquals(AwtWindowBackend.Other, AwtWindowBackend.fromToolkitClassName(name), name)
        }
    }

    @Test fun requestedToolkitDoesNotOverrideActualToolkitClassification() {
        withProperty("awt.toolkit.name", "XToolkit") {
            assertEquals(AwtWindowBackend.Wayland, AwtWindowBackend.fromToolkitClassName("sun.awt.wl.WLToolkit"))
        }
        withProperty("awt.toolkit.name", "WLToolkit") {
            assertEquals(AwtWindowBackend.X11, AwtWindowBackend.fromToolkitClassName("sun.awt.X11.XToolkit"))
        }
    }

    @Test fun nativeAutomaticScaleIgnoresXwaylandHints() {
        withProperty("abdm.systemUiScale", "auto") {
            withProperty("sun.java2d.uiScale", "1.75") {
                assertNull(SystemUiScale.detectDesktopScaleForBackend(AwtWindowBackend.Wayland))
            }
        }
    }

    @Test fun nativeExplicitScaleOverrideStillWorks() {
        withProperty("abdm.systemUiScale", "125%") {
            assertEquals(1.25f, SystemUiScale.detectDesktopScaleForBackend(AwtWindowBackend.Wayland))
        }
        withProperty("abdm.systemUiScale", "off") {
            assertNull(SystemUiScale.detectDesktopScaleForBackend(AwtWindowBackend.Wayland))
        }
    }

    private fun withProperty(key: String, value: String, block: () -> Unit) {
        val previous = System.getProperty(key)
        try {
            System.setProperty(key, value)
            block()
        } finally {
            if (previous == null) System.clearProperty(key) else System.setProperty(key, previous)
        }
    }
}
