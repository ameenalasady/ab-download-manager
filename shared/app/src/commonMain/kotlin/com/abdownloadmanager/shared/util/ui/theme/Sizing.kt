package com.abdownloadmanager.shared.util.ui.theme

import androidx.compose.runtime.*
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.TextUnit

val LocalSystemDensity = staticCompositionLocalOf<Density> {
    error("LocalSystemDensity not provided")
}

const val DEFAULT_UI_SCALE = 1f

val LocalUiScale = staticCompositionLocalOf<Float> { DEFAULT_UI_SCALE }

const val DEFAULT_FONT_SCALE = 1f

/**
 * How much text should deviate from the size the app defines for it, so that the app can follow
 * a system that asks for larger or smaller text without also inflating icons and paddings.
 */
val LocalFontScale = staticCompositionLocalOf<Float> { DEFAULT_FONT_SCALE }

val LocalTextSizes = compositionLocalOf<TextSizes> {
    error("LocalTextSizes not provided")
}

val myTextSizes
    @Composable
    get() = LocalTextSizes.current

@Stable
data class TextSizes(
    val xs: TextUnit,
    val sm: TextUnit,
    val base: TextUnit,
    val lg: TextUnit,
    val xl: TextUnit,
    val x2l: TextUnit,
    val x3l: TextUnit,
    val x4l: TextUnit,
    val x5l: TextUnit,
)

val LocalSpacing = compositionLocalOf<MySpacings> {
    error("LocalSpacing not provided")
}
val mySpacings
    @Composable
    get() = LocalSpacing.current


@Stable
data class MySpacings(
    val thumbSize: Dp,
    val iconSize: Dp,
    val smallSpace: Dp,
    val mediumSpace: Dp,
    val largeSpace: Dp,
)


/**
 * put this in every window because [Window] composable override [LocalDensity]
 */
@Composable
fun UiScaledContent(
    defaultDensity: Density = LocalDensity.current,
    uiScale: Float = LocalUiScale.current,
    fontScale: Float = LocalFontScale.current,
    content: @Composable () -> Unit,
) {
    val density = remember(defaultDensity, uiScale, fontScale) {
        if (uiScale == DEFAULT_UI_SCALE && fontScale == DEFAULT_FONT_SCALE) {
            defaultDensity
        } else {
            Density(
                density = uiScale * defaultDensity.density,
                // sp is multiplied by both, so the app's own text sizes keep following the ui
                // scale and only the extra deviation the system asks for is added on top
                fontScale = fontScale * defaultDensity.fontScale,
            )
        }
    }
    CompositionLocalProvider(
        LocalDensity provides density,
        content,
    )
}
