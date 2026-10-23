package com.abdownloadmanager.desktop.pages.settings

import androidx.compose.animation.AnimatedContent
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.verticalScroll
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.desktop.window.custom.WindowIcon
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.ui.configurable.RenderConfigurableGroup
import com.abdownloadmanager.shared.ui.widget.Handle
import com.abdownloadmanager.shared.ui.widget.Text
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.MultiplatformVerticalScrollbar
import com.abdownloadmanager.shared.util.ui.WithContentColor
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.needScroll
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import io.github.oikvpqya.compose.fastscroller.rememberScrollbarAdapter
import ir.amirab.util.compose.IconSource
import ir.amirab.util.compose.resources.myStringResource

@Composable
private fun SideBar(
    settingsComponent: DesktopSettingsComponent,
    modifier: Modifier = Modifier,
) {
    val currentPage by settingsComponent.currentPage.collectAsState()
    Column(
        modifier
            .fillMaxHeight()
            .background(myColors.surface / 0.2f)
            .verticalScroll(rememberScrollState())
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        Text(
            myStringResource(Res.string.settings),
            Modifier.padding(start = 12.dp, top = 4.dp, bottom = 12.dp),
            fontSize = myTextSizes.xl,
            fontWeight = FontWeight.SemiBold,
        )
        for (page in settingsComponent.pages) {
            SideBarItem(
                icon = page.icon,
                name = page.name.rememberString(),
                isSelected = currentPage == page,
                onClick = { settingsComponent.setCurrentPage(page) },
            )
        }
    }
}

@Composable
private fun SideBarItem(icon: IconSource, name: String, isSelected: Boolean, onClick: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .clip(myShapes.defaultRounded)
            .background(if (isSelected) myColors.primary / 0.14f else androidx.compose.ui.graphics.Color.Transparent)
            .selectable(selected = isSelected, onClick = onClick)
            .heightIn(min = 40.dp)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        WithContentColor(if (isSelected) myColors.primary else myColors.onBackground / 0.8f) {
            MyIcon(icon, null, Modifier.size(16.dp))
            Text(
                name,
                Modifier.weight(1f),
                fontWeight = if (isSelected) FontWeight.SemiBold else FontWeight.Normal,
                fontSize = myTextSizes.base,
                overflow = TextOverflow.Ellipsis,
                maxLines = 1,
            )
        }
    }
}

@Composable
fun SettingsPage(
    settingsComponent: DesktopSettingsComponent,
    onDismissRequest: () -> Unit,
) {
    WindowIcon(MyIcons.appIcon)
    val currentPage by settingsComponent.currentPage.collectAsState()
    val currentConfigurables by settingsComponent.configurables.collectAsState()
    Column(Modifier.fillMaxSize()) {
        Spacer(Modifier.fillMaxWidth().height(1.dp).background(myColors.onBackground / 0.08f))
        Row(Modifier.weight(1f)) {
            var sideBarWidth by remember { mutableStateOf(220.dp) }
            SideBar(settingsComponent, Modifier.width(sideBarWidth))
            Handle(Modifier.width(5.dp).fillMaxHeight()) {
                sideBarWidth = (sideBarWidth + it).coerceIn(180.dp..300.dp)
            }
            AnimatedContent(currentConfigurables, modifier = Modifier.weight(1f)) { configurableGroups ->
                val scrollState = rememberScrollState()
                val scrollbarAdapter = rememberScrollbarAdapter(scrollState)
                Row(Modifier.fillMaxSize()) {
                    Column(
                        Modifier
                            .weight(1f)
                            .verticalScroll(scrollState)
                            .padding(20.dp),
                        verticalArrangement = Arrangement.spacedBy(16.dp),
                    ) {
                        Text(
                            currentPage.name.rememberString(),
                            fontSize = myTextSizes.x3l,
                            fontWeight = FontWeight.SemiBold,
                        )
                        for (group in configurableGroups) {
                            RenderConfigurableGroup(
                                group,
                                Modifier.fillMaxWidth(),
                                itemPadding = PaddingValues(vertical = 10.dp, horizontal = 16.dp),
                                spaceBy = 0.dp,
                            )
                        }
                    }
                    if (scrollbarAdapter.needScroll()) {
                        MultiplatformVerticalScrollbar(
                            adapter = scrollbarAdapter,
                            modifier = Modifier.padding(vertical = 12.dp).padding(end = 4.dp),
                        )
                    }
                }
            }
        }
    }
}
