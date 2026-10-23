package com.abdownloadmanager.shared.ui.widget

import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.round
import androidx.compose.ui.window.PopupPositionProvider
import kotlin.math.roundToInt


/**
 * A [PopupPositionProvider] that positions the popup at the given position relative to the anchor.
 *
 * @param positionPx the offset, in pixels, relative to the anchor, to position the popup at.
 * @param offset [DpOffset] to be added to the position of the popup.
 * @param alignment The alignment of the popup relative to desired position.
 * @param windowMargin Defines the area within the window that limits the placement of the popup.
 */
@ExperimentalComposeUiApi
@Composable
fun rememberMyPopupPositionProviderAtPosition(
    positionPx: Offset,
    offset: DpOffset = DpOffset.Zero,
    alignment: Alignment = Alignment.BottomEnd,
    windowMargin: Dp = 4.dp
): PopupPositionProvider = with(LocalDensity.current) {
    val offsetPx = Offset(offset.x.toPx(), offset.y.toPx())
    val windowMarginPx = windowMargin.roundToPx()

    remember(positionPx, offsetPx, alignment, windowMarginPx) {
        PopupPositionProviderAtPosition(
            positionPx = positionPx,
            isRelativeToAnchor = true,
            offsetPx = offsetPx,
            alignment = alignment,
            windowMarginPx = windowMarginPx
        )
    }
}

/**
 * A [PopupPositionProvider] that positions the popup at the given offsets and alignment.
 *
 * @param positionPx The offset of the popup's location, in pixels.
 * @param isRelativeToAnchor Whether [positionPx] is relative to the anchor bounds passed to
 * [calculatePosition]. If `false`, it is relative to the window.
 * @param offsetPx Extra offset to be added to the position of the popup, in pixels.
 * @param alignment The alignment of the popup relative to desired position.
 * @param windowMarginPx Defines the area within the window that limits the placement of the popup,
 * in pixels.
 */
@ExperimentalComposeUiApi
class PopupPositionProviderAtPosition(
    val positionPx: Offset,
    val isRelativeToAnchor: Boolean,
    val offsetPx: Offset,
    val alignment: Alignment = Alignment.BottomEnd,
    val windowMarginPx: Int,
) : PopupPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize
    ): IntOffset {
        val anchor = IntRect(
            offset = positionPx.round() +
                    (if (isRelativeToAnchor) anchorBounds.topLeft else IntOffset.Zero),
            size = IntSize.Zero
        )
        val tooltipArea = IntRect(
            IntOffset(
                anchor.left - popupContentSize.width,
                anchor.top - popupContentSize.height,
            ),
            IntSize(
                popupContentSize.width * 2,
                popupContentSize.height * 2
            )
        )
        val position = alignment.align(popupContentSize, tooltipArea.size, layoutDirection)
        var x = tooltipArea.left + position.x + offsetPx.x
        var y = tooltipArea.top + position.y + offsetPx.y
        if (x + popupContentSize.width > windowSize.width - windowMarginPx) {
            x -= popupContentSize.width
        }
        if (y + popupContentSize.height > windowSize.height - windowMarginPx) {
            y -= popupContentSize.height + anchor.height
        }
        x = x.coerceAtLeast(windowMarginPx.toFloat())
        y = y.coerceAtLeast(windowMarginPx.toFloat())

        return IntOffset(x.roundToInt(), y.roundToInt())
    }
}

/**
 * Provides [PopupPositionProvider] relative to the current component bounds.
 *
 * @param anchor The anchor point relative to the current component bounds.
 * @param alignment The alignment of the popup relative to the [anchor] point.
 * @param offset [DpOffset] to be added to the position of the popup.
 */
@Composable
fun rememberMyComponentRectPositionProvider(
    anchor: Alignment = Alignment.BottomCenter,
    alignment: Alignment = Alignment.BottomCenter,
    offset: DpOffset = DpOffset.Zero
): PopupPositionProvider {
    val offsetPx = with(LocalDensity.current) {
        IntOffset(offset.x.roundToPx(), offset.y.roundToPx())
    }
    val marginPx = with(LocalDensity.current) { 4.dp.roundToPx() }
    return remember(anchor, alignment, offsetPx, marginPx) {
        ComponentPopupPositionProvider(anchor, alignment, offsetPx, marginPx = marginPx)
    }
}

@Composable
fun rememberMyComponentCustomRectPositionProvider(
    providedAnchorBounds: IntRect,
    anchor: Alignment = Alignment.BottomCenter,
    alignment: Alignment = Alignment.BottomCenter,
    offset: DpOffset = DpOffset.Zero
): PopupPositionProvider {
    val offsetPx = with(LocalDensity.current) {
        IntOffset(offset.x.roundToPx(), offset.y.roundToPx())
    }
    val marginPx = with(LocalDensity.current) { 4.dp.roundToPx() }
    return remember(providedAnchorBounds, anchor, alignment, offsetPx, marginPx) {
        ComponentPopupPositionProvider(anchor, alignment, offsetPx, providedAnchorBounds, marginPx)
    }
}

/**
 * Measurement counterpart to below-first anchored placement. A picker may use its
 * control's width or a preferred panel width; both stay within the viewport. Its
 * scrollable content fits on at least one side without covering the anchor.
 * All arguments are in layout pixels, including density-scaled margin and gap.
 */
fun anchoredPopupConstraints(
    anchorBounds: IntRect,
    windowSize: IntSize,
    preferredWidth: Int? = null,
    marginPx: Int = 4,
    gapPx: Int = 4,
): IntSize {
    val availableWidth = (windowSize.width - 2 * marginPx).coerceAtLeast(1)
    val above = anchorBounds.top - marginPx - gapPx
    val below = windowSize.height - anchorBounds.bottom - marginPx - gapPx
    return IntSize(
        (preferredWidth ?: anchorBounds.width).coerceIn(1, availableWidth),
        maxOf(above, below).coerceAtLeast(1),
    )
}

/** Keep anchored menus reachable at the window edges, including RTL and cascading menus. */
internal class ComponentPopupPositionProvider(
    private val anchor: Alignment,
    private val alignment: Alignment,
    private val offsetPx: IntOffset,
    private val providedAnchorBounds: IntRect? = null,
    private val marginPx: Int = 4,
) : PopupPositionProvider {
    override fun calculatePosition(
        anchorBounds: IntRect,
        windowSize: IntSize,
        layoutDirection: LayoutDirection,
        popupContentSize: IntSize,
    ): IntOffset {
        val bounds = providedAnchorBounds ?: anchorBounds
        val point = anchor.align(IntSize.Zero, bounds.size, layoutDirection)
        val area = IntSize(popupContentSize.width * 2, popupContentSize.height * 2)
        val position = alignment.align(popupContentSize, area, layoutDirection)
        val initial = bounds.topLeft + point - IntOffset(popupContentSize.width, popupContentSize.height) + position + offsetPx
        val margin = marginPx
        val maxX = (windowSize.width - popupContentSize.width - margin).coerceAtLeast(margin)
        val maxY = (windowSize.height - popupContentSize.height - margin).coerceAtLeast(margin)
        var x = initial.x
        var y = initial.y
        val isCascade = anchor == Alignment.TopEnd && alignment == Alignment.BottomEnd
        if (isCascade) {
            if (x > maxX) x = bounds.left - popupContentSize.width - offsetPx.x
            else if (x < margin) x = bounds.right + offsetPx.x
        }
        if (!isCascade) {
            if (y > maxY) {
                val above = bounds.top - popupContentSize.height - offsetPx.y
                if (above >= margin) y = above
            } else if (y < margin) {
                val below = bounds.bottom + offsetPx.y
                if (below <= maxY) y = below
            }
        }
        return IntOffset(x.coerceIn(margin, maxX), y.coerceIn(margin, maxY))
    }
}
