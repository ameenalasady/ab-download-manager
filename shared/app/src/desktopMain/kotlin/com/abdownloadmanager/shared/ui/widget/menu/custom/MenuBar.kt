package com.abdownloadmanager.shared.ui.widget.menu.custom

import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import ir.amirab.util.compose.action.MenuItem
import androidx.compose.foundation.*
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsHoveredAsState
import androidx.compose.foundation.layout.*
import com.abdownloadmanager.shared.ui.widget.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.semantics.Role
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import androidx.compose.ui.unit.dp

@Composable
fun MenuBar(
    modifier: Modifier = Modifier,
    subMenuList: List<MenuItem.SubMenu>,
) {
    var openedItem: MenuItem.SubMenu? by remember {
        mutableStateOf(null)
    }
    val onRequestClose = {
        openedItem = null
    }
    Row(
        verticalAlignment = Alignment.CenterVertically,
    ) {
        for (subMenu in subMenuList) {
            val isSelected = openedItem == subMenu
            val interactionSource = remember { MutableInteractionSource() }
            val isHovered by interactionSource.collectIsHoveredAsState()
            LaunchedEffect(isHovered) {
                if (isHovered && openedItem != null) {
                    openedItem = subMenu
                }
            }
            Column {
                Column(
                    modifier
                        .clip(myShapes.defaultRounded)
                        .hoverable(interactionSource)
                        .background(if (isSelected || isHovered) myColors.onBackground / 0.08f else androidx.compose.ui.graphics.Color.Transparent)
                        .clickable(role = Role.Button) {
                            openedItem = subMenu.takeUnless { isSelected }
                        }
                        .padding(horizontal = 10.dp, vertical = 6.dp)
                        .wrapContentHeight(Alignment.CenterVertically)
                ) {
                    val text = subMenu.title.collectAsState().value.rememberString()
                    Text(
                        text = text,
                        maxLines = 1,
                        fontSize = myTextSizes.base,
                        color = myColors.onBackground,
                    )
                }
                if (isSelected) {
                    MyDropDown(
                        onDismissRequest = onRequestClose,
                        focusable = true,
                    ) {
                        SubMenu(subMenu, onRequestClose = onRequestClose)
                    }
                }
            }
        }
    }
}
