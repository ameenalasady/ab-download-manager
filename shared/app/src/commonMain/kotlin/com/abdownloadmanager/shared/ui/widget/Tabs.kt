package com.abdownloadmanager.shared.ui.widget

import ir.amirab.util.compose.IconSource
import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import com.abdownloadmanager.shared.util.ui.WithContentAlpha
import androidx.compose.foundation.background
import androidx.compose.foundation.selection.selectable
import androidx.compose.ui.semantics.Role
import com.abdownloadmanager.shared.util.div
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import ir.amirab.util.compose.StringSource
import ir.amirab.util.ifThen


@Composable
fun MyTabRow(content: @Composable RowScope.() -> Unit) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
    ) {
        content()
    }
}

@Composable
fun MyTab(
    selected: Boolean,
    onClick: () -> Unit,
    icon: IconSource,
    title: StringSource,
    selectionBackground: Color = myColors.primary / 0.1f,
) {
    WithContentAlpha(
        if (selected) 1f else 0.75f
    ) {
        Box(
            Modifier
                .ifThen(selected) { background(selectionBackground) }
                .selectable(selected = selected, role = Role.Tab, onClick = onClick)
                .height(IntrinsicSize.Min),
        ) {
            Row(
                Modifier.heightIn(min = 40.dp).padding(horizontal = 16.dp, vertical = 10.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                MyIcon(icon, null, Modifier.size(16.dp))
                Spacer(Modifier.width(8.dp))
                Text(
                    title.rememberString(),
                    maxLines = 1,
                    fontSize = myTextSizes.base,
                    fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
                )
            }
            if (selected) {
                Box(Modifier.matchParentSize(), contentAlignment = Alignment.BottomCenter) {
                    Spacer(Modifier.fillMaxWidth().height(2.dp).background(myColors.primary))
                }
            }
        }
    }
}
