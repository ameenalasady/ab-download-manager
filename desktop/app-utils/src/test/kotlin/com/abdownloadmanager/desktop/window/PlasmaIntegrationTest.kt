package com.abdownloadmanager.desktop.window

import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.desktop.window.custom.titlebar.SystemButtonType
import com.abdownloadmanager.desktop.window.custom.titlebar.SystemButtonsPosition
import ir.amirab.util.desktop.screen.SystemUiScale
import ir.amirab.util.desktop.screen.applyUiScale
import ir.amirab.util.desktop.screen.unApplyUiScale
import java.io.File
import kotlin.test.*

class PlasmaIntegrationTest {
    private val provider = Class.forName(
        "com.abdownloadmanager.desktop.window.custom.titlebar.linux.LinuxSystemButtonsProviderKt"
    )

    private fun parseKde(contents: String): SystemButtonsPosition? {
        val file = File.createTempFile("abdm-kwin", ".ini")
        try {
            file.writeText(contents)
            val method = provider.getDeclaredMethod("parseKDELayout", File::class.java)
            method.isAccessible = true
            return method.invoke(null, file) as? SystemButtonsPosition
        } finally {
            file.delete()
        }
    }

    @Test fun plasmaDecorationVersionsAndDefaultButtons() {
        for (version in listOf("", "2", "3")) {
            val position = assertNotNull(parseKde("""
                [Unrelated]
                ButtonsOnRight=X
                [org.kde.kdecoration$version]
                ButtonsOnLeft=MS
                ButtonsOnRight=HIAX
            """.trimIndent()))
            assertFalse(position.isLeft)
            assertEquals(listOf(SystemButtonType.Minimize, SystemButtonType.Maximize, SystemButtonType.Close), position.buttons)
        }
    }

    @Test fun leftHandCloseButtonLayout() {
        val position = assertNotNull(parseKde("""
            [org.kde.kdecoration2]
            ButtonsOnLeft=XAI
            ButtonsOnRight=MS
        """.trimIndent()))
        assertTrue(position.isLeft)
        assertEquals(listOf(SystemButtonType.Close, SystemButtonType.Maximize, SystemButtonType.Minimize), position.buttons)
    }

    @Test fun missingDecorationIsSafe() {
        assertNull(parseKde("[Other]\nButtonsOnRight=IAX"))
    }

    @Test fun fractionalWindowSizeRoundTrips() {
        val size = DpSize(451.25.dp, 239.75.dp)
        for (scale in listOf(1f, 1.25f, 1.5f, 1.75f, 2f)) {
            val restored = size.applyUiScale(scale).unApplyUiScale(scale)
            assertEquals(size.width.value, restored.width.value, 0.001f)
            assertEquals(size.height.value, restored.height.value, 0.001f)
        }
        assertEquals(DpSize.Unspecified, DpSize.Unspecified.applyUiScale(1.25f))
    }

    @Test fun hungSettingsCommandReallyTimesOut() {
        val method = provider.getDeclaredMethod("runCommand", Array<String>::class.java)
        method.isAccessible = true
        val started = System.nanoTime()
        assertNull(method.invoke(null, arrayOf("sh", "-c", "exec sleep 10") as Any))
        assertTrue((System.nanoTime() - started) / 1_000_000 < 4000)
    }

    @Test fun invalidScaleOverridesDoNotReachDensity() {
        val key = "abdm.systemUiScale"
        val previous = System.getProperty(key)
        val method = SystemUiScale::class.java.getDeclaredMethod("detectDesktopScale")
        method.isAccessible = true
        try {
            for (invalid in listOf("NaN", "Infinity", "-1", "99", "off")) {
                System.setProperty(key, invalid)
                assertNull(method.invoke(SystemUiScale), invalid)
            }
            System.setProperty(key, "125%")
            assertEquals(1.25f, method.invoke(SystemUiScale))
        } finally {
            if (previous == null) System.clearProperty(key) else System.setProperty(key, previous)
        }
    }
}
