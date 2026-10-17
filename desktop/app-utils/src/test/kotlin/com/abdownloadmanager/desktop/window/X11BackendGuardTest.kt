package com.abdownloadmanager.desktop.window

import com.abdownloadmanager.desktop.window.linux.X11Ewmh
import com.abdownloadmanager.desktop.window.linux.canEwmhMove
import ir.amirab.util.desktop.window.AwtWindowBackend
import java.awt.GraphicsEnvironment
import kotlin.test.*

class X11BackendGuardTest {
    @Test fun headlessToolkitNeverAttemptsXOpenDisplayEvenForNonzeroHandles() {
        // The Gradle test task explicitly selects headless AWT, irrespective of DISPLAY.
        assertTrue(GraphicsEnvironment.isHeadless())
        assertEquals(AwtWindowBackend.Other, AwtWindowBackend.current)
        assertFalse(canEwmhMove())
        assertFalse(X11Ewmh.supportsMoveResize())
        assertFalse(X11Ewmh.isFullscreen(0x1234L))
        assertFalse(X11Ewmh.setFullscreen(0x1234L, false))
        assertFalse(X11Ewmh.sendMoveResize(0x1234L, 0, 0))
        assertFalse(X11Ewmh.unmap(0x1234L))
        X11Ewmh.sync()
        val tried = X11Ewmh::class.java.getDeclaredField("displayTried").apply { isAccessible = true }
        assertFalse(tried.getBoolean(X11Ewmh))
    }
}
