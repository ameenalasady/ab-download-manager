package com.abdownloadmanager.desktop.ui.template

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myShapes

/** One surface treatment for auxiliary dialogs, pickers, and expanded form sections. */
@Composable
fun Modifier.dialogSurface(shape: Shape = myShapes.defaultRounded): Modifier =
    clip(shape)
        .border(1.dp, myColors.menuBorderColor, shape)
        .background(myColors.surface)
