package com.abdownloadmanager.desktop.pages.addDownload.shared

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.selectable
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.foundation.relocation.BringIntoViewRequester
import androidx.compose.foundation.relocation.bringIntoViewRequester
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.desktop.ui.template.OptionPopup
import com.abdownloadmanager.desktop.ui.template.OptionPopupAnchor
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.ui.widget.Text
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import ir.amirab.util.compose.resources.myStringResource

@Composable
fun <T> DialogDropDown(
    selectedItem: T?,
    possibleItems: List<T>,
    onItemSelected: (T) -> Unit,
    modifier: Modifier,
    enabled: Boolean = true,
    dropdownOpen: Boolean,
    onRequestOpenDropDown: () -> Unit,
    onRequestCloseDropDown: () -> Unit,
    renderItem: @Composable (T) -> Unit,
    renderEmpty: @Composable () -> Unit,
) {
    OptionPopupAnchor(modifier) {
        DropDownHeader(selectedItem, enabled, onRequestOpenDropDown, renderItem)
        if (dropdownOpen) {
            OptionPopup(onDismissRequest = onRequestCloseDropDown, matchAnchorWidth = true) {
                if (possibleItems.isEmpty()) {
                    Box(Modifier.fillMaxWidth().padding(12.dp)) { renderEmpty() }
                }
                possibleItems.forEach { item ->
                    val selected = item == selectedItem
                    val bringIntoView = remember { BringIntoViewRequester() }
                    LaunchedEffect(selected) {
                        if (selected) bringIntoView.bringIntoView()
                    }
                    Row(
                        Modifier.fillMaxWidth()
                            .bringIntoViewRequester(bringIntoView)
                            .clip(myShapes.defaultRounded)
                            .background(if (selected) myColors.primary / 0.12f else Color.Transparent)
                            .heightIn(min = 36.dp)
                            .selectable(selected = selected, onClick = {
                                onItemSelected(item)
                                onRequestCloseDropDown()
                            })
                            .padding(horizontal = 12.dp, vertical = 8.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Box(Modifier.weight(1f)) { renderItem(item) }
                        Spacer(Modifier.width(8.dp))
                        if (selected) MyIcon(MyIcons.check, null, Modifier.size(16.dp))
                        else Spacer(Modifier.size(16.dp))
                    }
                }
            }
        }
    }
}

@Composable
private fun <T> DropDownHeader(
    item: T?,
    enabled: Boolean,
    onClick: () -> Unit,
    renderItem: @Composable (T) -> Unit,
) {
    val borderColor = myColors.onBackground / 0.1f
    val shape = myShapes.defaultRounded
    Row(
        Modifier.fillMaxWidth()
            .heightIn(min = 36.dp)
            .height(IntrinsicSize.Max)
            .clip(shape)
            .alpha(if (enabled) 1f else 0.5f)
            .border(1.dp, borderColor, shape)
            .background(myColors.surface / 50)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        val contentModifier = Modifier.padding(vertical = 8.dp).weight(1f)
        if (item != null) Box(contentModifier) { renderItem(item) }
        else Text(myStringResource(Res.string.no_category_selected), contentModifier)
        Spacer(Modifier.padding(horizontal = 8.dp).fillMaxHeight().padding(vertical = 1.dp)
            .width(1.dp).background(borderColor))
        MyIcon(MyIcons.down, null, Modifier.size(16.dp))
    }
}
