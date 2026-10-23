package com.abdownloadmanager.desktop.pages.home

import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import ir.amirab.util.ifThen
import com.abdownloadmanager.shared.ui.widget.menu.custom.MyDropDown
import com.abdownloadmanager.shared.ui.widget.menu.custom.SubMenu
import ir.amirab.util.compose.action.MenuItem
import com.abdownloadmanager.shared.util.div
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import com.abdownloadmanager.shared.ui.widget.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsFocusedAsState
import androidx.compose.foundation.interaction.collectIsHoveredAsState
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.semantics.Role
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.shared.ui.widget.Tooltip
import com.abdownloadmanager.shared.util.ui.MultiplatformHorizontalScrollbar
import com.abdownloadmanager.shared.util.ui.needScroll
import ir.amirab.util.compose.IconSource
import ir.amirab.util.compose.StringSource

@Composable
fun Actions(
    list: List<MenuItem>,
    showLabels: Boolean,
    modifier: Modifier = Modifier,
) {
    val scrollState = rememberScrollState()
    Column(modifier) {
        Row(
            Modifier
                .height(IntrinsicSize.Max)
                .horizontalScroll(scrollState),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            for (a in list) {
                when (a) {
                    MenuItem.Separator -> {
                        Spacer(
                            Modifier
                                .padding(horizontal = 6.dp)
                                .fillMaxHeight()
                                .padding(vertical = 4.dp)
                                .width(1.dp)
                                .background(myColors.onBackground / 0.1f)
                        )
                    }

                    is MenuItem.SingleItem -> {
                        ActionButton(Modifier, a, showLabels)
                    }

                    is MenuItem.SubMenu -> {
                        GroupActionButton(Modifier, a, showLabels)
                    }
                }
            }
        }
        val adapter = rememberScrollbarAdapter(scrollState)
        if (adapter.needScroll()) {
            MultiplatformHorizontalScrollbar(
                adapter = adapter,
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 2.dp),
            )
        }
    }
}

@Composable
private fun ActionButton(
    modifier: Modifier = Modifier,
    action: MenuItem.SingleItem,
    showLabels: Boolean,
) {
    val enabled by action.isEnabled.collectAsState()
    Column(modifier) {
        ActionIconWithLabel(
            title = action.title.collectAsState().value,
            icon = action.icon.collectAsState().value,
            showLabels = showLabels,
            onClick = {
                action()
            },
            enabled = enabled
        )
    }
}

@Composable
private fun GroupActionButton(
    modifier: Modifier = Modifier,
    action: MenuItem.SubMenu,
    showLabels: Boolean,
) {
    val enabled by action.isEnabled.collectAsState()
    var showSubMenu by remember { mutableStateOf(false) }
    Column(modifier) {
        ActionIconWithLabel(
            title = action.title.collectAsState().value,
            icon = action.icon.collectAsState().value,
            showLabels = showLabels,
            onClick = {
                showSubMenu = !showSubMenu
            },
            enabled = enabled
        )
        val close = {
            showSubMenu = false
        }
        if (enabled && showSubMenu) {
            MyDropDown(onDismissRequest = close) {
                val items by action.items.collectAsState()
                SubMenu(subMenu = items, onRequestClose = close)
            }
        }
    }
}

@Composable
private fun ActionIconWithLabel(
    title: StringSource,
    icon: IconSource?,
    showLabels: Boolean,
    onClick: () -> Unit,
    enabled: Boolean,
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isHovered by interactionSource.collectIsHoveredAsState()
    val isFocused by interactionSource.collectIsFocusedAsState()
    OptionalTooltip(title.takeIf { !showLabels }) {
        Row(
            modifier = Modifier
                .heightIn(min = 36.dp)
                .clip(myShapes.defaultRounded)
                .background(if (enabled && (isHovered || isFocused)) myColors.onBackground / 0.08f else Color.Transparent)
                .border(1.dp, if (isFocused) myColors.primary else Color.Transparent, myShapes.defaultRounded)
                .clickable(
                    enabled = enabled,
                    interactionSource = interactionSource,
                    indication = LocalIndication.current,
                    role = Role.Button,
                    onClick = onClick,
                )
                .ifThen(!enabled) { alpha(0.4f) }
                .padding(horizontal = 8.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            icon?.let {
                MyIcon(it, if (showLabels) null else title.rememberString(), Modifier.size(16.dp))
            }
            if (showLabels) {
                Spacer(Modifier.width(6.dp))
                Text(title.rememberString(), maxLines = 1, fontSize = myTextSizes.base)
            }
        }
    }

}

@Composable
private fun OptionalTooltip(
    tooltip: StringSource?,
    content: @Composable () -> Unit
) {
    if (tooltip != null) {
        Tooltip(tooltip) {
            content()
        }
    } else {
        content()
    }
}
