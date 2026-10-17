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
    fun explicitAnchorBoundsAreHonored() {
        val provider = ComponentPopupPositionProvider(
            Alignment.BottomStart, Alignment.BottomEnd, IntOffset.Zero, IntRect(300, 200, 400, 240),
        )
        assertEquals(IntOffset(300, 240), provider.calculatePosition(
            IntRect(0, 0, 20, 20), window, LayoutDirection.Ltr, menu,
        ))
    }
}
