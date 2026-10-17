package com.abdownloadmanager.desktop.ui.template

import androidx.compose.foundation.layout.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.shared.ui.widget.ActionContainer

@Composable
fun DialogUi(
    mainContent: @Composable () -> Unit,
    footer: @Composable () -> Unit,
) {
    Column {
        Box(Modifier.weight(1f)) {
            mainContent()
        }
        footer()
    }
}

@Composable
fun DialogMainContent(
    contentPadding: PaddingValues = PaddingValues(
        top = 16.dp,
        bottom = 0.dp,
        start = 16.dp,
        end = 16.dp,
    ),
    content: @Composable () -> Unit,
) {
    Column(
        Modifier.padding(contentPadding)
    ) {
        content()
    }
}

@Composable
fun DialogFooter(
    content: @Composable () -> Unit,
) {
    ActionContainer(Modifier.fillMaxWidth(), content = content)
}
