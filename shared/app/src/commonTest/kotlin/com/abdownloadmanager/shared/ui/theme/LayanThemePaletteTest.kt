package com.abdownloadmanager.shared.ui.theme

import androidx.compose.ui.graphics.Color
import kotlin.math.max
import kotlin.math.min
import kotlin.math.pow
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class LayanThemePaletteTest {
    private val colors = LayanTheme.colors

    @Test
    fun isRegisteredAsASelectableDarkTheme() {
        assertEquals("layan", colors.id)
        assertEquals("Layan", colors.name)
        assertFalse(colors.isLight)
        assertEquals(
            listOf(colors),
            DefaultThemes.getAll().filter { it.id == colors.id },
        )
    }

    @Test
    fun surfacesAreOpaqueAndDistinct() {
        assertEquals(1f, colors.background.alpha)
        assertEquals(1f, colors.surface.alpha)
        assertNotEquals(colors.background, colors.surface)
    }

    @Test
    fun primaryAndSecondaryReadAsTextOnSurfaces() {
        assertContrastAtLeast(4.5, colors.primary, colors.background, "primary on background")
        assertContrastAtLeast(4.5, colors.primary, colors.surface, "primary on surface")
        assertContrastAtLeast(4.5, colors.secondary, colors.background, "secondary on background")
        assertContrastAtLeast(4.5, colors.secondary, colors.surface, "secondary on surface")
    }

    @Test
    fun textReadsOnSurfaces() {
        assertContrastAtLeast(4.5, colors.onBackground, colors.background, "onBackground on background")
        assertContrastAtLeast(4.5, colors.onSurface, colors.surface, "onSurface on surface")
    }

    @Test
    fun semanticColorsReadOnSurfaces() {
        val semanticColors = listOf(
            "error" to colors.error,
            "warning" to colors.warning,
            "success" to colors.success,
            "info" to colors.info,
        )
        for ((name, color) in semanticColors) {
            assertContrastAtLeast(4.5, color, colors.background, "$name on background")
            assertContrastAtLeast(4.5, color, colors.surface, "$name on surface")
        }
    }

    @Test
    fun onColorsReadOnTheirSemanticBackgrounds() {
        val onColorPairs = listOf(
            "onPrimary" to (colors.onPrimary to colors.primary),
            "onSecondary" to (colors.onSecondary to colors.secondary),
            "onError" to (colors.onError to colors.error),
            "onWarning" to (colors.onWarning to colors.warning),
            "onSuccess" to (colors.onSuccess to colors.success),
            "onInfo" to (colors.onInfo to colors.info),
        )
        for ((name, pair) in onColorPairs) {
            assertContrastAtLeast(4.5, pair.first, pair.second, "$name on its background")
        }
    }

    @Test
    fun directedBrandPurpleIsRetainedAsPrimaryVariant() {
        assertEquals(Color(0xFF5657F5), colors.primaryVariant)
        assertContrastBelow(4.5, colors.primaryVariant, colors.background, "directed purple on background")
    }

    @Test
    fun primaryIsLightenedBecauseItIsAlsoUsedAsText() {
        assertContrastAtLeast(4.5, colors.primary, colors.background, "primary on background")
        assertNotEquals(colors.primaryVariant, colors.primary)
    }

    private fun assertContrastAtLeast(
        minimum: Double,
        foreground: Color,
        background: Color,
        label: String,
    ) {
        val actual = contrastRatio(foreground, background)
        assertTrue(
            actual >= minimum,
            "$label expected contrast >= $minimum but was $actual",
        )
    }

    private fun assertContrastBelow(
        maximum: Double,
        foreground: Color,
        background: Color,
        label: String,
    ) {
        val actual = contrastRatio(foreground, background)
        assertTrue(
            actual < maximum,
            "$label expected contrast < $maximum but was $actual",
        )
    }

    private fun contrastRatio(foreground: Color, background: Color): Double {
        val foregroundLuminance = relativeLuminance(foreground)
        val backgroundLuminance = relativeLuminance(background)
        val lighter = max(foregroundLuminance, backgroundLuminance)
        val darker = min(foregroundLuminance, backgroundLuminance)
        return (lighter + 0.05) / (darker + 0.05)
    }

    private fun relativeLuminance(color: Color): Double {
        fun linear(channel: Float): Double {
            val value = channel.toDouble()
            return if (value <= 0.04045) {
                value / 12.92
            } else {
                ((value + 0.055) / 1.055).pow(2.4)
            }
        }
        return 0.2126 * linear(color.red) +
                0.7152 * linear(color.green) +
                0.0722 * linear(color.blue)
    }
}
