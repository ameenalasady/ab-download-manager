package com.abdownloadmanager.desktop.window

import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import java.awt.GraphicsConfiguration
import java.awt.GraphicsEnvironment
import java.awt.Insets
import java.awt.Rectangle
import java.awt.Toolkit

/**
 * The desktop is not the same between two runs of the app: a laptop gets undocked from a big
 * monitor, a screen is rotated, the scale changes. Restoring a window blindly can then place it
 * outside of every screen, or make it larger than the desktop - and a window that asks for more
 * space than the work area is what most window managers end up showing as a maximized window.
 *
 * Everything here is in the unit compose uses for
 * [androidx.compose.ui.window.WindowState], which on awt is a pixel of the user space
 * coordinate system, so it can be compared with awt screen bounds directly.
 */
object ScreenGeometry {
    /** a window is considered lost when less than this much of it is on a screen */
    private val MIN_VISIBLE = 64.dp

    data class Area(
        val left: Dp,
        val top: Dp,
        val right: Dp,
        val bottom: Dp,
    ) {
        val width get() = right - left
        val height get() = bottom - top
    }

    /**
     * The area of a screen a window may use, panels and docks excluded.
     * @param near a point the screen should contain, the primary screen is used when it is not
     * on any of them.
     */
    fun usableArea(near: DpOffset? = null): Area? {
        val configuration = configurationAt(near) ?: return null
        val bounds = runCatching { configuration.bounds }.getOrNull() ?: return null
        val insets = runCatching {
            Toolkit.getDefaultToolkit().getScreenInsets(configuration)
        }.getOrElse { Insets(0, 0, 0, 0) }
        val area = Area(
            left = (bounds.x + insets.left).dp,
            top = (bounds.y + insets.top).dp,
            right = (bounds.x + bounds.width - insets.right).dp,
            bottom = (bounds.y + bounds.height - insets.bottom).dp,
        )
        // some compositors report nonsense insets over xwayland, in which case the raw
        // bounds are still a much better answer than an empty area
        if (area.width <= 0.dp || area.height <= 0.dp) {
            return Area(
                left = bounds.x.dp,
                top = bounds.y.dp,
                right = (bounds.x + bounds.width).dp,
                bottom = (bounds.y + bounds.height).dp,
            )
        }
        return area
    }

    /**
     * @return [size] shrunk to fit on the screen, never below [minimumSize] and never bigger
     * than what was asked for.
     */
    fun fitSize(
        size: DpSize,
        minimumSize: DpSize,
        near: DpOffset? = null,
    ): DpSize {
        val sanitized = DpSize(
            width = size.width.sanitize(minimumSize.width),
            height = size.height.sanitize(minimumSize.height),
        )
        val area = usableArea(near) ?: return sanitized
        return DpSize(
            width = sanitized.width.coerceAtMost(area.width).coerceAtLeast(minimumSize.width),
            height = sanitized.height.coerceAtMost(area.height).coerceAtLeast(minimumSize.height),
        )
    }

    /**
     * @return [position] moved so that the window is reachable again, or `null` when there is no
     * screen to put it on and the platform should decide where the window goes.
     */
    fun fitPosition(
        position: DpOffset,
        size: DpSize,
    ): DpOffset? {
        if (!position.x.value.isFinite() || !position.y.value.isFinite()) return null
        if (isVisibleOnAnyScreen(position, size)) return position
        val area = usableArea() ?: return null
        return DpOffset(
            x = position.x.coerceIn(area.left, (area.right - size.width).coerceAtLeast(area.left)),
            y = position.y.coerceIn(area.top, (area.bottom - size.height).coerceAtLeast(area.top)),
        )
    }

    private fun isVisibleOnAnyScreen(position: DpOffset, size: DpSize): Boolean {
        val window = Rectangle(
            position.x.value.toInt(),
            position.y.value.toInt(),
            size.width.value.toInt().coerceAtLeast(1),
            size.height.value.toInt().coerceAtLeast(1),
        )
        val minVisible = MIN_VISIBLE.value.toInt()
        return configurations().any { configuration ->
            val visible = runCatching { configuration.bounds }.getOrNull()?.intersection(window)
                ?: return@any false
            visible.width >= minVisible.coerceAtMost(window.width) &&
                visible.height >= minVisible.coerceAtMost(window.height)
        }
    }

    private fun configurationAt(point: DpOffset?): GraphicsConfiguration? {
        val configurations = configurations()
        if (configurations.isEmpty()) return null
        if (point != null) {
            val x = point.x.value.toInt()
            val y = point.y.value.toInt()
            configurations
                .firstOrNull { runCatching { it.bounds.contains(x, y) }.getOrDefault(false) }
                ?.let { return it }
        }
        return primaryConfiguration() ?: configurations.first()
    }

    private fun configurations(): List<GraphicsConfiguration> {
        return runCatching {
            GraphicsEnvironment.getLocalGraphicsEnvironment()
                .screenDevices
                .map { it.defaultConfiguration }
        }.getOrElse { emptyList() }
    }

    private fun primaryConfiguration(): GraphicsConfiguration? {
        return runCatching {
            GraphicsEnvironment.getLocalGraphicsEnvironment()
                .defaultScreenDevice
                .defaultConfiguration
        }.getOrNull()
    }

    private fun Dp.sanitize(fallback: Dp): Dp {
        return takeIf { it.value.isFinite() && it.value > 0f } ?: fallback
    }
}
