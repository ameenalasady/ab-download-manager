package com.abdownloadmanager.desktop.pages.settings

import com.abdownloadmanager.desktop.window.custom.CustomWindow
import com.abdownloadmanager.shared.util.mvi.HandleEffects
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import com.abdownloadmanager.desktop.window.ScreenGeometry
import com.abdownloadmanager.desktop.window.bringToFront
import com.abdownloadmanager.shared.util.ui.theme.LocalUiScale
import ir.amirab.util.desktop.screen.applyUiScale
import ir.amirab.util.desktop.screen.unApplyUiScale
import androidx.compose.ui.window.WindowPlacement
import androidx.compose.ui.window.v2.WindowBoundsProvider
import androidx.compose.ui.window.v2.WindowPositionProvider
import androidx.compose.ui.window.v2.WindowSizeProvider
import androidx.compose.ui.window.v2.rememberWindowState
import com.abdownloadmanager.desktop.AppComponent
import com.abdownloadmanager.desktop.window.custom.isMinimizedOrNull
import com.abdownloadmanager.desktop.window.custom.placementOrNull
import com.abdownloadmanager.desktop.window.custom.sizeOrNull
import com.abdownloadmanager.desktop.window.custom.rememberWindowController
import com.abdownloadmanager.shared.settings.BaseSettingsComponent
import com.abdownloadmanager.shared.util.rememberChild
import com.abdownloadmanager.resources.Res
import ir.amirab.util.compose.resources.myStringResource

@Composable
fun SettingWindow(
    appComponent: AppComponent,
) {
    appComponent.showSettingSlot.rememberChild()?.let {
        SettingWindow(it, appComponent::closeSettings)
    }
}

@Composable
private fun SettingWindow(
    settingsComponent: DesktopSettingsComponent,
    onRequestCloseWindow: () -> Unit,
) {
    val uiScale = LocalUiScale.current
    val minimumSize = DpSize(500.dp, 350.dp).applyUiScale(uiScale)
    val initialSize = remember(settingsComponent, uiScale) {
        ScreenGeometry.fitSize(settingsComponent.windowSize.value.applyUiScale(uiScale), minimumSize)
    }
    val windowState = rememberWindowState(
        initialBoundsProvider = WindowBoundsProvider(
            sizeProvider = WindowSizeProvider.Fixed(
                size = initialSize,
            ),
            positionProvider = WindowPositionProvider.CenteredOnScreen
        )
    )
    LaunchedEffect(windowState.sizeOrNull, uiScale) {
        if (windowState.isMinimizedOrNull == false && windowState.placementOrNull == WindowPlacement.Floating) {
            windowState.sizeOrNull?.let {
                if (it.width >= minimumSize.width && it.height >= minimumSize.height) {
                    settingsComponent.setWindowSize(it.unApplyUiScale(uiScale))
                }
            }
        }
    }
    CustomWindow(
        state = windowState,
        minSize = minimumSize,
        onCloseRequest = {
            onRequestCloseWindow()
        },
        windowController = rememberWindowController(
            title = myStringResource(Res.string.settings),
        ),
    ) {
        HandleEffects(settingsComponent) {
            when (it) {
                is BaseSettingsComponent.Effects.Platform -> {
                    when (it as DesktopSettingsComponent.Effects) {
                        DesktopSettingsComponent.Effects.BringToFront -> {
                            windowState.requestMinimized(false)
                            window.bringToFront()
                        }
                    }
                }
            }
        }
//        Spacer(Modifier.fillMaxWidth().height(1.dp).background(myColors.surface))
        SettingsPage(settingsComponent, onRequestCloseWindow)
    }
}
