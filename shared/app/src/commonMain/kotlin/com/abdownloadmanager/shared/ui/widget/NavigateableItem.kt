package com.abdownloadmanager.shared.ui.widget

import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.WithContentAlpha
import androidx.compose.foundation.background
import androidx.compose.foundation.selection.selectable
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.unit.dp

@Composable
fun NavigateableItem(
    isSelected:Boolean,
    onClick:()->Unit,
    content:@Composable ()->Unit,
){
    val shape = myShapes.defaultRounded
    WithContentAlpha(if (isSelected)1f else 0.75f){
        Row(
            Modifier
                .fillMaxWidth()
                .clip(shape)
                .background(if (isSelected) myColors.primary / 0.14f else androidx.compose.ui.graphics.Color.Transparent)
                .selectable(selected = isSelected, onClick = onClick)
                .heightIn(min = 40.dp)
                .padding(horizontal = 12.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            content()
        }
    }
}
