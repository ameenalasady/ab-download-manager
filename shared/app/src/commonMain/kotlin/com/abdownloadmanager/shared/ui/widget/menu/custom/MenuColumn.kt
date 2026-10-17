package com.abdownloadmanager.shared.ui.widget.menu.custom

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.div

@Composable
fun MenuColumn(
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    val shape = LocalMenuBoxClip.current
    Column(
        modifier
            .shadow(12.dp, shape)
            .widthIn(min = 200.dp, max = 480.dp)
            .width(IntrinsicSize.Max)
            .clip(shape)
            .border(1.dp, myColors.menuBorderColor, shape)
            .background(myColors.menuGradientBackground)
            .padding(4.dp)
            .heightIn(max = 560.dp)
            .verticalScroll(rememberScrollState())
    ) {
        content()
    }
}
