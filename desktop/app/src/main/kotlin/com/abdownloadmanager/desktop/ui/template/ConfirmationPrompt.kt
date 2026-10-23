package com.abdownloadmanager.desktop.ui.template

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.abdownloadmanager.resources.Res
import com.abdownloadmanager.shared.ui.widget.ActionButton
import com.abdownloadmanager.shared.ui.widget.PrimaryMainActionButton
import com.abdownloadmanager.shared.ui.widget.Text
import com.abdownloadmanager.shared.util.ui.myColors
import com.abdownloadmanager.shared.util.ui.theme.myShapes
import com.abdownloadmanager.shared.util.ui.theme.myTextSizes
import ir.amirab.util.compose.resources.myStringResource

/** Shared confirmation layout; destructive actions never receive the initial keyboard focus. */
@Composable
fun ConfirmationPrompt(
    title: String,
    confirmText: String,
    onConfirm: () -> Unit,
    onCancel: () -> Unit,
    destructive: Boolean = false,
    content: @Composable ColumnScope.() -> Unit,
) {
    Dialog(onDismissRequest = onCancel) {
        val shape = myShapes.defaultRounded
        Column(
            Modifier.widthIn(min = 320.dp, max = 420.dp).width(IntrinsicSize.Max)
                .dialogSurface(shape)
                .padding(24.dp),
        ) {
            Text(title, fontSize = myTextSizes.xl, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(16.dp))
            content()
            Spacer(Modifier.height(24.dp))
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.End)) {
                val initialFocus = remember { FocusRequester() }
                LaunchedEffect(Unit) { initialFocus.requestFocus() }
                ActionButton(
                    text = myStringResource(Res.string.cancel),
                    modifier = if (destructive) Modifier.focusRequester(initialFocus) else Modifier,
                    onClick = onCancel,
                )
                if (destructive) {
                    ActionButton(
                        text = confirmText,
                        onClick = onConfirm,
                        contentColor = myColors.error,
                        borderColor = SolidColor(myColors.error),
                        focusedBorderColor = SolidColor(myColors.error),
                    )
                } else {
                    PrimaryMainActionButton(
                        text = confirmText,
                        modifier = Modifier.focusRequester(initialFocus),
                        onClick = onConfirm,
                    )
                }
            }
        }
    }
}
