package com.abdownloadmanager.desktop.pages.home

import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.runtime.*
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.WindowPlacement
import androidx.compose.ui.window.v2.WindowBoundsProvider
import androidx.compose.ui.window.v2.WindowPositionProvider
import androidx.compose.ui.window.v2.WindowSizeProvider
import androidx.compose.ui.window.v2.rememberWindowState
import com.abdownloadmanager.desktop.AppComponent
import com.abdownloadmanager.desktop.utils.AppInfo
import com.abdownloadmanager.desktop.window.ScreenGeometry
import com.abdownloadmanager.desktop.window.bringToFront
import com.abdownloadmanager.desktop.window.custom.CustomWindow
import com.abdownloadmanager.desktop.window.custom.rememberWindowController
import com.abdownloadmanager.shared.util.LocalShortCutManager
import com.abdownloadmanager.shared.util.mvi.HandleEffects
import com.abdownloadmanager.shared.util.rememberChild
import com.abdownloadmanager.shared.util.ui.icon.MyIcons
import com.abdownloadmanager.shared.util.ui.theme.LocalUiScale
import ir.amirab.util.desktop.screen.applyUiScale
import ir.amirab.util.desktop.screen.unApplyUiScale
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach

private val MINIMUM_SIZE = DpSize(400.dp, 400.dp)

@Composable
fun HomeWindow(appComponent: AppComponent) {
    appComponent.showHomeSlot.rememberChild()?.let {
        HomeWindow(it, appComponent::closeHome)
    }
}

@Composable
private fun HomeWindow(homeComponent: HomeComponent, onCLoseRequest: () -> Unit) {
    val uiScale = LocalUiScale.current
    val minimumSize = MINIMUM_SIZE.applyUiScale(uiScale)
    val initialSize = remember(homeComponent, uiScale) {
        ScreenGeometry.fitSize(
            size = homeComponent.windowSize.value.applyUiScale(uiScale),
            minimumSize = minimumSize,
            near = homeComponent.windowPosition.value,
        )
    }
    val initialPosition = remember(homeComponent, uiScale) {
        homeComponent.windowPosition.value
            ?.let { ScreenGeometry.fitPosition(it, initialSize) }
            ?.let { WindowPositionProvider.Absolute(it) }
            ?: WindowPositionProvider.CenteredOnScreen
    }
    val windowState = rememberWindowState(
        initialBoundsProvider = WindowBoundsProvider(
            sizeProvider = WindowSizeProvider.Fixed(size = initialSize),
            positionProvider = initialPosition,
        ),
        initialPlacement = if (homeComponent.isMaximized.value) {
            WindowPlacement.Maximized
        } else {
            WindowPlacement.Floating
        },
    )
    val windowController = rememberWindowController(
        AppInfo.displayName,
        MyIcons.appIcon.rememberPainter(),
    )
    CompositionLocalProvider(LocalShortCutManager provides homeComponent.shortcutManager) {
        CustomWindow(
            state = windowState,
            onCloseRequest = onCLoseRequest,
            windowController = windowController,
            onKeyEvent = { homeComponent.shortcutManager.handle(it) },
            minSize = minimumSize,
        ) {
            LaunchedEffect(windowState, uiScale) {
                snapshotFlow {
                    // v2 properties throw until initialization has finished.
                    if (!windowState.isInitialized) null else Geometry(
                        windowState.placement,
                        windowState.isMinimized,
                        windowState.size,
                        windowState.position,
                    )
                }.filterNotNull().onEach { geometry ->
                    homeComponent.setIsMaximized(geometry.placement == WindowPlacement.Maximized)
                    if (geometry.isMinimized || geometry.placement != WindowPlacement.Floating) return@onEach
                    if (geometry.size.width < minimumSize.width || geometry.size.height < minimumSize.height) return@onEach
                    homeComponent.setWindowSize(geometry.size.unApplyUiScale(uiScale))
                    homeComponent.setWindowPosition(geometry.position)
                }.launchIn(this)
            }
            HandleEffects(homeComponent) {
                when (it) {
                    HomeComponent.Effects.BringToFront -> {
                        windowState.requestMinimized(false)
                        window.bringToFront()
                    }
                    else -> {}
                }
            }
            BoxWithConstraints { HomePage(homeComponent) }
        }
    }
}

private data class Geometry(
    val placement: WindowPlacement,
    val isMinimized: Boolean,
    val size: DpSize,
    val position: DpOffset,
)
