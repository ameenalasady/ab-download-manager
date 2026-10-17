package com.abdownloadmanager.shared.ui.widget

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.myColors
import ir.amirab.util.compose.IconSource
import ir.amirab.util.compose.StringSource


@Composable
fun PrimaryMainIconButton(
    icon: IconSource,
    contentDescription: StringSource,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    onClick: () -> Unit,
) {
    val backgroundColor = SolidColor(myColors.primary / 0.18f)
    val borderColor = SolidColor(myColors.primary / 0.7f)
    val disabledBorderColor = SolidColor(myColors.primary / 0.25f)
    IconActionButtonWithBrush(
        icon = icon,
        contentDescription = contentDescription,
        modifier = modifier,
        enabled = enabled,
        onClick = onClick,
        backgroundColor = backgroundColor,
        disabledBackgroundColor = backgroundColor,
        borderColor = borderColor,
        disabledBorderColor = disabledBorderColor,
    )
}
