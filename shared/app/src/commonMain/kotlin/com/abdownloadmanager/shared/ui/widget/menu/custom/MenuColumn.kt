package com.abdownloadmanager.shared.ui.widget.menu.custom

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.rememberScrollbarAdapter
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.shared.util.ui.MultiplatformVerticalScrollbar
import com.abdownloadmanager.shared.util.ui.myColors

@Composable
fun MenuColumn(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    val shape = LocalMenuBoxClip.current
    val scrollState = rememberScrollState()
    Box(
        modifier
            .shadow(12.dp, shape)
            .widthIn(min = 200.dp, max = 480.dp)
            .width(IntrinsicSize.Max)
            .clip(shape)
            .border(1.dp, myColors.menuBorderColor, shape)
            .background(myColors.menuGradientBackground)
            .padding(4.dp)
            .heightIn(max = 560.dp)
    ) {
        Column(Modifier.verticalScroll(scrollState), content = content)
        if (scrollState.maxValue > 0) {
            MultiplatformVerticalScrollbar(
                rememberScrollbarAdapter(scrollState),
                Modifier.matchParentSize().wrapContentWidth(Alignment.End),
            )
        }
    }
}
