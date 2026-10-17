package com.abdownloadmanager.shared.ui.widget

import androidx.compose.ui.Alignment
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import kotlin.test.Test
import kotlin.test.assertEquals

class ComponentPopupPositionProviderTest {
    private val window = IntSize(800, 600)
    private val menu = IntSize(200, 120)

    private fun dropdown() = ComponentPopupPositionProvider(
        Alignment.BottomStart, Alignment.BottomEnd, IntOffset(0, 4),
    )

    @Test
    fun opensBelowAndAlignsWithLeadingEdge() {
        assertEquals(IntOffset(100, 144), dropdown().calculatePosition(
            IntRect(100, 100, 260, 140), window, LayoutDirection.Ltr, menu,
        ))
    }

    @Test
    fun alignsWithLeadingEdgeInRtl() {
        assertEquals(IntOffset(160, 144), dropdown().calculatePosition(
            IntRect(200, 100, 360, 140), window, LayoutDirection.Rtl, menu,
        ))
    }

    @Test
    fun flipsAboveFooterInsteadOfClippingTheChoices() {
        assertEquals(IntOffset(100, 416), dropdown().calculatePosition(
            IntRect(100, 540, 260, 580), window, LayoutDirection.Ltr, menu,
        ))
    }

    @Test
    fun keepsDropdownInsideRightEdge() {
        assertEquals(IntOffset(596, 144), dropdown().calculatePosition(
            IntRect(720, 100, 780, 140), window, LayoutDirection.Ltr, menu,
        ))
    }

    @Test
    fun cascadingMenuFlipsToLeftAtRightEdge() {
        val provider = ComponentPopupPositionProvider(Alignment.TopEnd, Alignment.BottomEnd, IntOffset.Zero)
        assertEquals(IntOffset(340, 100), provider.calculatePosition(
            IntRect(540, 100, 780, 136), window, LayoutDirection.Ltr, menu,
        ))
    }

    @Test
    fun cascadingMenuFlipsToRightAtLeftEdgeInRtl() {
        val provider = ComponentPopupPositionProvider(Alignment.TopEnd, Alignment.BottomEnd, IntOffset.Zero)
        assertEquals(IntOffset(250, 100), provider.calculatePosition(
            IntRect(10, 100, 250, 136), window, LayoutDirection.Rtl, menu,
        ))
    }

    @Test
    fun oversizedPopupNeverProducesNegativeCoordinates() {
        assertEquals(IntOffset(4, 4), dropdown().calculatePosition(
            IntRect(10, 10, 40, 40), IntSize(100, 80), LayoutDirection.Ltr, menu,
        ))
    }

    @Test
    fun categoryPickerFitsBelowItsFieldAtDefaultAddDownloadSize() {
        val anchor = IntRect(148, 88, 536, 124)
        val size = anchoredPopupConstraints(anchor, IntSize(600, 340))
        assertEquals(IntOffset(148, 128), dropdown().calculatePosition(
            anchor, IntSize(600, 340), LayoutDirection.Ltr, size,
        ))
    }

    @Test
    fun footerPickerFallsBackAboveWithoutCoveringItsTriggerAtMultipleUiScales() {
        for (scale in listOf(1, 2, 3)) {
            val provider = ComponentPopupPositionProvider(
                Alignment.BottomStart, Alignment.BottomEnd, IntOffset(0, 4 * scale), marginPx = 4 * scale,
            )
            assertEquals(IntOffset(20 * scale, 122 * scale), provider.calculatePosition(
                IntRect(20 * scale, 294 * scale, 300 * scale, 328 * scale),
                IntSize(600 * scale, 340 * scale),
                LayoutDirection.Ltr,
                IntSize(280 * scale, 168 * scale),
            ))
        }
    }

    @Test
    fun pickerMeasurementMatchesTriggerAtFractionalAndLargeDensities() {
        for (scale in listOf(1.0, 1.25, 1.5, 2.0)) {
            fun px(value: Int) = (value * scale).toInt()
            val anchor = IntRect(px(148), px(90), px(540), px(126))
            val size = anchoredPopupConstraints(anchor, IntSize(px(600), px(340)),
                marginPx = px(4), gapPx = px(4))
            assertEquals(anchor.width, size.width)
            assertEquals(px(340) - anchor.bottom - px(4) * 2, size.height)
            val provider = ComponentPopupPositionProvider(
                Alignment.BottomStart, Alignment.BottomEnd, IntOffset(0, px(4)), marginPx = px(4),
            )
            assertEquals(IntOffset(anchor.left, anchor.bottom + px(4)), provider.calculatePosition(
                anchor, IntSize(px(600), px(340)), LayoutDirection.Ltr, size,
            ))
        }
    }

    @Test
    fun panelWidthIsLimitedByViewportRatherThanTrigger() {
        assertEquals(IntSize(292, 192), anchoredPopupConstraints(
            IntRect(260, 200, 292, 232), IntSize(300, 400), preferredWidth = 340,
        ))
    }

    @Test
    fun longPickerScrollsOnLargerSideWithoutOverlappingAnchor() {
        val anchor = IntRect(100, 240, 300, 276)
        val size = anchoredPopupConstraints(anchor, IntSize(600, 400))
        assertEquals(IntSize(200, 232), size)
        assertEquals(IntOffset(100, 4), dropdown().calculatePosition(
            anchor, IntSize(600, 400), LayoutDirection.Ltr, size,
        ))
    }

    @Test
    fun solutionsPreferBelowWhenWindowHasRoom() {
        assertEquals(IntOffset(20, 264), dropdown().calculatePosition(
            IntRect(20, 224, 300, 260), window, LayoutDirection.Ltr, IntSize(280, 168),
        ))
    }

    @Test
    fun pickerTracksControlBoundsAfterLayoutChanges() {
        val provider = ComponentPopupPositionProvider(
            Alignment.BottomStart, Alignment.BottomEnd, IntOffset.Zero,
        )
        val before = provider.calculatePosition(
            IntRect(152, 80, 580, 116), window, LayoutDirection.Ltr, menu,
        )
        val after = provider.calculatePosition(
            IntRect(192, 96, 620, 132), window, LayoutDirection.Ltr, menu,
        )
        assertEquals(IntOffset(40, 16), after - before)
    }

    @Test
    fun explicitAnchorBoundsAreHonored() {
        val provider = ComponentPopupPositionProvider(
            Alignment.BottomStart, Alignment.BottomEnd, IntOffset.Zero, IntRect(300, 200, 400, 240),
        )
        assertEquals(IntOffset(300, 240), provider.calculatePosition(
            IntRect(0, 0, 20, 20), window, LayoutDirection.Ltr, menu,
        ))
    }
}
