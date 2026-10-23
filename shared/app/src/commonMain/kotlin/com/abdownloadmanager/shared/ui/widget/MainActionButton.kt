package com.abdownloadmanager.shared.ui.widget

import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.SolidColor
import com.abdownloadmanager.shared.util.div
import com.abdownloadmanager.shared.util.ui.myColors


@Composable
fun PrimaryMainActionButton(
    text: String,
    modifier: Modifier,
    enabled: Boolean = true,
    onLongClick: (() -> Unit)? = null,
    onClick: () -> Unit,
) {
    val backgroundColor = SolidColor(myColors.primary / 0.18f)
    val borderColor = SolidColor(myColors.primary / 0.7f)
    val disabledBorderColor = SolidColor(myColors.primary / 0.25f)
    ActionButton(
        text = text,
        modifier = modifier,
        enabled = enabled,
        onClick = onClick,
        onLongClick = onLongClick,
        backgroundColor = backgroundColor,
        disabledBackgroundColor = backgroundColor,
        borderColor = borderColor,
        disabledBorderColor = disabledBorderColor,
    )
}
