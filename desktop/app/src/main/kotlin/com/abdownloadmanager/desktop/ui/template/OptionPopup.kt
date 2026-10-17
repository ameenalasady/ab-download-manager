package com.abdownloadmanager.desktop.ui.template

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.boundsInWindow
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.shared.ui.widget.menu.custom.MenuColumn
import com.abdownloadmanager.shared.ui.widget.menu.custom.MyDropDown
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.IntRect
import androidx.compose.ui.unit.roundToIntRect
import com.abdownloadmanager.shared.ui.widget.anchoredPopupConstraints

private val LocalOptionAnchor = staticCompositionLocalOf { IntRect.Zero }

/** Measure the actual control, not the page or mouse position (also on native Wayland). */
@Composable
fun OptionPopupAnchor(modifier: Modifier = Modifier, content: @Composable BoxScope.() -> Unit) {
    var anchor by remember { mutableStateOf(IntRect.Zero) }
    Box(modifier.onGloballyPositioned {
        anchor = it.boundsInWindow().roundToIntRect()
    }) {
        CompositionLocalProvider(LocalOptionAnchor provides anchor) { content() }
    }
}

/**
 * Fields match their trigger; small icon/button triggers use a readable content panel.
 * Below first, above if necessary. Limit height to the larger free region so a long
 * picker scrolls instead of being clamped over its control. No global coordinates or
 * top-level dialogs: layout, UI scaling and the renderer's viewport stay authoritative.
 */
@Composable
fun OptionPopup(
    onDismissRequest: () -> Unit,
    modifier: Modifier = Modifier,
    matchAnchorWidth: Boolean = false,
    content: @Composable () -> Unit,
) {
    val anchor = LocalOptionAnchor.current
    val density = LocalDensity.current
    val windowSize = LocalWindowInfo.current.containerSize
    val constraints = with(density) {
        anchoredPopupConstraints(
            anchor, windowSize,
            preferredWidth = if (matchAnchorWidth) null else 340.dp.roundToPx(),
            marginPx = 4.dp.roundToPx(), gapPx = 4.dp.roundToPx(),
        )
    }
    val width = with(density) { constraints.width.toDp() }
    val availableHeight = with(density) { constraints.height.toDp() }
    MyDropDown(onDismissRequest = onDismissRequest, offset = DpOffset(0.dp, 4.dp)) {
        MenuColumn(
            modifier = modifier
                .width(width)
                .heightIn(max = availableHeight),
        ) { content() }
    }
}
