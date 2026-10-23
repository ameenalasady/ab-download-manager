package com.abdownloadmanager.desktop.window

import androidx.compose.ui.Alignment
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.window.PopupPositionProviderAtPosition
import androidx.compose.ui.unit.dp
import ir.amirab.util.desktop.GlobalLayoutDirection
import java.awt.MouseInfo
import java.awt.Window

/**
 * Places this window where the mouse is, which is where the user just clicked to open it.
 *
 * There is no global pointer position under wayland - a client is only told about the pointer
 * while it is over one of its own surfaces - so awt hands us nothing there and we have to settle
 * for the middle of the window this one belongs to.
 */
fun Window.moveNearMouse(
    alignment: Alignment = Alignment.BottomEnd,
) {
    val mousePosition = currentMousePosition()
    if (mousePosition == null) {
        // centers on the screen when there is no owner
        setLocationRelativeTo(owner)
        return
    }
    moveSafe(mousePosition, alignment)
}

/** @return where the pointer is in desktop coordinates, or `null` when the platform will not say */
fun currentMousePosition(): DpOffset? {
    val location = runCatching {
        MouseInfo.getPointerInfo()?.location
    }.getOrNull() ?: return null
    return DpOffset(location.x.dp, location.y.dp)
}

/**
 * Places this window next to [position], pulling it back inside the screen when it would not
 * fit there.
 *
 * [position] is in absolute desktop coordinates, so the window is kept inside the screen that
 * actually holds that point - on a second monitor the primary screen says nothing useful about
 * where the window may go.
 */
fun Window.moveSafe(
    position: DpOffset,
    alignment: Alignment = Alignment.BottomEnd,
) {
    val area = ScreenGeometry.usableArea(position) ?: return
    val screenOrigin = IntOffset(area.left.value.toInt(), area.top.value.toInt())
    val positionProvider = PopupPositionProviderAtPosition(
        positionPx = Offset.Zero,
        isRelativeToAnchor = true,
        offsetPx = Offset.Zero,
        alignment = alignment,
        windowMarginPx = 0,
    )
    val anchor = IntOffset(
        x = position.x.value.toInt() - screenOrigin.x,
        y = position.y.value.toInt() - screenOrigin.y,
    )
    val offset = positionProvider.calculatePosition(
        popupContentSize = IntSize(width, height),
        layoutDirection = GlobalLayoutDirection,
        windowSize = IntSize(area.width.value.toInt(), area.height.value.toInt()),
        anchorBounds = IntRect(anchor, IntSize.Zero),
    )
    setLocation(
        offset.x + screenOrigin.x,
        offset.y + screenOrigin.y,
    )
}
