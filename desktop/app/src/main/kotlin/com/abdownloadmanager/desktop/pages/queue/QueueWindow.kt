package com.abdownloadmanager.desktop.pages.queue

import androidx.compose.runtime.Composable
import androidx.compose.ui.window.v2.rememberWindowState
import com.abdownloadmanager.desktop.AppComponent
import com.abdownloadmanager.desktop.window.bringToFront
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.v2.WindowBoundsProvider
import androidx.compose.ui.window.v2.WindowSizeProvider
import com.abdownloadmanager.shared.util.ui.theme.LocalUiScale
import ir.amirab.util.desktop.screen.applyUiScale
import com.abdownloadmanager.desktop.window.custom.CustomWindow
import com.abdownloadmanager.desktop.window.custom.rememberWindowController
import com.abdownloadmanager.shared.util.mvi.HandleEffects
import com.abdownloadmanager.shared.util.rememberChild
import com.abdownloadmanager.resources.Res
import ir.amirab.util.compose.resources.myStringResource

@Composable
fun QueuesWindow(appComponent: AppComponent) {
    appComponent.showQueuesSlot.rememberChild()?.let {
        QueuesWindow(it)
    }
}


@Composable
private fun QueuesWindow(queuesComponent: QueuesComponent) {
    val state = rememberWindowState(
        initialBoundsProvider = WindowBoundsProvider(
            sizeProvider = WindowSizeProvider.Fixed(
                size = DpSize(800.dp, 600.dp).applyUiScale(LocalUiScale.current)
            )
        )
    )
    CustomWindow(
        state = state,
        onCloseRequest = queuesComponent.close,
        windowController = rememberWindowController(
            title = myStringResource(Res.string.queues),
        ),
    ) {
        HandleEffects(queuesComponent) {
            if (it == QueuesComponentEffects.ToFront) {
                state.requestMinimized(false)
                window.bringToFront()
            }
        }
        QueuePage(queuesComponent)
    }
}
