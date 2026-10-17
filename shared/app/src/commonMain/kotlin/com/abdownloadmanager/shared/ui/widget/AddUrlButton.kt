package com.abdownloadmanager.shared.ui.widget

import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.widget.MyIcon
import ir.amirab.util.compose.resources.myStringResource

@Composable
fun AddUrlButton(
    modifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    ActionButton(
        text = myStringResource(Res.string.new_download),
        modifier = modifier,
        onClick = onClick,
        backgroundColor = SolidColor(myColors.primary / 0.18f),
        borderColor = SolidColor(myColors.primary / 0.7f),
        contentColor = myColors.onBackground,
        start = {
            MyIcon(MyIcons.add, null, Modifier.size(16.dp))
            Spacer(Modifier.width(8.dp))
        },
    )
}
