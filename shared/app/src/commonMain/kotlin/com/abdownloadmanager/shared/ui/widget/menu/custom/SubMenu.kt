package com.abdownloadmanager.shared.ui.widget.menu.custom

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.hoverable
import androidx.compose.foundation.focusable
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsHoveredAsState
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.input.key.*
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.shared.ui.widget.Text
import com.abdownloadmanager.shared.util.LocalShortCutManager
import com.abdownloadmanager.shared.util.PlatformKeyStroke
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.ProvideTextStyle
import com.abdownloadmanager.shared.util.ui.WithContentColor
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import ir.amirab.util.compose.action.MenuItem
import ir.amirab.util.compose.modifiers.autoMirror
import ir.amirab.util.ifThen
import kotlinx.coroutines.delay

enum class MenuDisabledItemBehavior {
    Filter,
    LowerOpacity,
}

val LocalMenuDisabledItemBehavior = compositionLocalOf {
    MenuDisabledItemBehavior.LowerOpacity
}
val LocalMenuBoxClip = compositionLocalOf<Shape> {
    RoundedCornerShape(8.dp)
}

/**
 * render a menu
 */
@Composable
fun SubMenu(
    subMenu: MenuItem.SubMenu,
    onRequestClose: () -> Unit,
    onNavigateBack: (() -> Unit)? = null,
    header: (@Composable () -> Unit)? = null,
) {
    SubMenu(
        subMenu = subMenu.items.collectAsState().value,
        header = header,
        onRequestClose = onRequestClose,
        onNavigateBack = onNavigateBack,
    )
}
@Composable
fun SubMenu(
    subMenu: List<MenuItem>,
    onRequestClose: () -> Unit,
    onNavigateBack: (() -> Unit)? = null,
    header: (@Composable () -> Unit)? = null,
) {
    var openedItem: MenuItem.SubMenu? by remember {
        mutableStateOf(null)
    }
    var lastHoveredItem by remember {
        mutableStateOf(null as MenuItem?)
    }

    var keyboardItem: MenuItem? by remember { mutableStateOf(null) }
    val focusRequester = remember { FocusRequester() }
    val enabledItems = subMenu.filter { item ->
        item != MenuItem.Separator && ((item as? MenuItem.HasEnable)?.isEnabled?.collectAsState()?.value != false)
    }
    LaunchedEffect(Unit) { focusRequester.requestFocus() }
    WithContentColor(myColors.onMenuColor) {
        MenuColumn(
            Modifier
                .onPreviewKeyEvent { event ->
                    if (event.type != KeyEventType.KeyDown) return@onPreviewKeyEvent false
                    when (event.key) {
                        Key.DirectionDown, Key.DirectionUp, Key.MoveHome, Key.MoveEnd -> {
                            if (enabledItems.isNotEmpty()) {
                                val current = enabledItems.indexOf(keyboardItem ?: lastHoveredItem)
                                val index = when (event.key) {
                                    Key.MoveHome -> 0
                                    Key.MoveEnd -> enabledItems.lastIndex
                                    Key.DirectionUp -> if (current <= 0) enabledItems.lastIndex else current - 1
                                    else -> (current + 1) % enabledItems.size
                                }
                                keyboardItem = enabledItems[index]
                                openedItem = null
                            }
                            true
                        }
                        Key.Enter, Key.NumPadEnter, Key.DirectionRight -> {
                            val item = keyboardItem ?: lastHoveredItem ?: enabledItems.firstOrNull()
                            when (item) {
                                is MenuItem.SubMenu -> if (item in enabledItems) openedItem = item
                                is MenuItem.SingleItem -> if (event.key != Key.DirectionRight && item in enabledItems) {
                                    if (item.shouldDismissOnClick) onRequestClose()
                                    item.onClick()
                                }
                                else -> Unit
                            }
                            true
                        }
                        Key.DirectionLeft -> { (onNavigateBack ?: onRequestClose)(); true }
                        Key.Escape -> { onRequestClose(); true }
                        else -> false
                    }
                }
                .focusRequester(focusRequester)
                .focusable(),
        ) {
            header?.invoke()
            for (menuItem in subMenu) {
                val interactionSource = remember { MutableInteractionSource() }
                val bringIntoView = remember { BringIntoViewRequester() }
                val isHovered by interactionSource.collectIsHoveredAsState()
                LaunchedEffect(keyboardItem) {
                    if (keyboardItem == menuItem) bringIntoView.bringIntoView()
                }
                LaunchedEffect(isHovered) {
                    if (isHovered) {
                        keyboardItem = null
                        lastHoveredItem = menuItem
                    }
                }
                RenderMenuItem(
                    menuItem = menuItem,
                    openedItem = openedItem,
                    onRequestCLose = onRequestClose,
                    isSelected = openedItem == menuItem || keyboardItem == menuItem,
                    onRequestOpenItem = {
                        openedItem = it
                    },
                    isHovered = lastHoveredItem == menuItem,
                    modifier = Modifier.hoverable(interactionSource).bringIntoViewRequester(bringIntoView)
                )
            }
        }
    }
}

@Composable
private fun ReactableItem(
    item: MenuItem.ReadableItem,
    onClick: () -> Unit,
    isSelected: Boolean,
    modifier: Modifier = Modifier,
    interactionSource: MutableInteractionSource = remember { MutableInteractionSource() },
    extraContent: @Composable () -> Unit = {},
) {
    val iconModifier = Modifier.size(16.dp)
    val title by item.title.collectAsState()
    val icon by item.icon.collectAsState()
    val itemPadding = Modifier.padding(horizontal = 10.dp, vertical = 8.dp)
    val isHovered by interactionSource.collectIsHoveredAsState()
    val isEnabled = (item as? MenuItem.HasEnable)
        ?.isEnabled
        ?.collectAsState()
        ?.value ?: true
    Row(
        modifier
            .heightIn(min = 36.dp)
            .clip(RoundedCornerShape(4.dp))
            .ifThen(!isEnabled) { alpha(0.45f) }
            .hoverable(interactionSource)
            .background(
                when {
                    (isHovered && isEnabled) || isSelected -> {
                        myColors.onMenuColor / 0.08f
                    }

                    else -> {
                        Color.Transparent
                    }
                }
            )
            .clickable(enabled = isEnabled, role = Role.Button) {
                onClick()
            }
            .then(itemPadding)
            .fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically
    ) {
        // Keep the label gutter identical for items with and without an icon.
        Box(iconModifier, contentAlignment = Alignment.Center) {
            icon?.let { MyIcon(it, null, iconModifier) }
        }
        Spacer(Modifier.width(12.dp))
        Text(
            title.rememberString(),
            Modifier.weight(1f),
            fontSize = myTextSizes.base,
            softWrap = false,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        Spacer(Modifier.width(16.dp))
        extraContent()
    }
}

@Composable
private fun RenderMenuItem(
    menuItem: MenuItem,
    openedItem: MenuItem.SubMenu?,
    onRequestCLose: () -> Unit,
    isSelected: Boolean,
    isHovered: Boolean,
    modifier: Modifier = Modifier,
    onRequestOpenItem: (MenuItem.SubMenu?) -> Unit,
) {
//    val isEnabled by menuItem.isEnabled.collectAsState()
    val isEnabled = (menuItem as? MenuItem.HasEnable)?.isEnabled?.collectAsState()?.value ?: true
    LaunchedEffect(isHovered, menuItem, isEnabled) {
        if (isHovered) {
            // A short delay makes it possible to cross other rows on the way to a submenu.
            delay(150)
            onRequestOpenItem((menuItem as? MenuItem.SubMenu)?.takeIf { isEnabled })
        }
    }
    Row(
        modifier
            .fillMaxWidth()
    ) {
        when (menuItem) {
            MenuItem.Separator -> {
                Spacer(
                    Modifier
                        .padding(horizontal = 8.dp, vertical = 4.dp)
                        .fillMaxWidth()
                        .height(1.dp)
                        .background(myColors.menuBorderColor)
                )
            }

            is MenuItem.SingleItem -> {
                RenderSingleItem(
                    item = menuItem,
                    isSelected = isSelected,
                    onRequestClose = onRequestCLose,
                )
            }

            is MenuItem.SubMenu -> {
                RenderSubMenuItem(
                    menuItem = menuItem,
                    isSelected = isSelected,
                    onRequestCLose = onRequestCLose,
                    openedItem = openedItem,
                    onRequestOpenItem = onRequestOpenItem,
                )
            }
        }
    }
}

@Composable
private fun RenderSubMenuItem(
    menuItem: MenuItem.SubMenu,
    isSelected: Boolean,
    openedItem: MenuItem.SubMenu?,
    onRequestOpenItem: (MenuItem.SubMenu?) -> Unit,
    onRequestCLose: () -> Unit,
) {
    ReactableItem(
        item = menuItem,
        onClick = {
            onRequestOpenItem(menuItem)
        },
        isSelected = isSelected,
        extraContent = {
            MyIcon(
                MyIcons.next,
                null,
                Modifier
                    .size(16.dp)
                    .autoMirror(),
            )
        })
    if (openedItem == menuItem) {
        SiblingDropDown(
            onDismissRequest = {
                onRequestOpenItem(null)
            }
        ) {
            SubMenu(menuItem, onRequestCLose, onNavigateBack = { onRequestOpenItem(null) })
        }
    }
}

@Composable
private fun RenderSingleItem(
    onRequestClose: () -> Unit,
    isSelected: Boolean,
    item: MenuItem.SingleItem,
) {
    val isEnabled by item.isEnabled.collectAsState()
    if (!isEnabled && LocalMenuDisabledItemBehavior.current == MenuDisabledItemBehavior.Filter) {
        return
    }

    val shortcutManager = LocalShortCutManager.current
    val shortcutStroke = remember(shortcutManager, item) {
        shortcutManager?.getShortCutOf(item)
    }
    val onClick = {
        if (item.shouldDismissOnClick) {
            onRequestClose()
        }
        item.onClick()
    }
    ReactableItem(
        item = item,
        onClick = onClick,
        isSelected = isSelected,
        extraContent = {
            if (shortcutStroke != null) {
                RenderShortcutStroke(shortcutStroke)
            }
        }
    )

}

@Composable
private fun RenderShortcutStroke(shortcutStroke: PlatformKeyStroke) {
    val modifiers = remember(shortcutStroke) {
        buildList {
            addAll(shortcutStroke.getModifiers())
            add(shortcutStroke.getKeyText())
        }
    }
    ProvideTextStyle(
        TextStyle(
            fontSize = myTextSizes.sm,
        )
    ) {
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(1.dp)
        ) {
            WithContentColor(myColors.onMenuColor / 0.65f) {
                Text(modifiers.joinToString("+"), maxLines = 1)
            }
        }
    }
}
