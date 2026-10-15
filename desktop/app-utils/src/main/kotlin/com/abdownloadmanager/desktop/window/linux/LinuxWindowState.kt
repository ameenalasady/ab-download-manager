package com.abdownloadmanager.desktop.window.linux

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.remember
import androidx.compose.ui.awt.ComposeWindow
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.isPrimaryPressed
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.window.FrameWindowScope
import ir.amirab.util.platform.Platform
import ir.amirab.util.platform.isLinux
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withTimeoutOrNull
import java.awt.Frame
import java.awt.MouseInfo
import java.awt.Window
import java.awt.event.ComponentAdapter
import java.awt.event.ComponentEvent
import java.awt.event.WindowStateListener

/**
 * Whether the window manager currently shows [window] fullscreen.
 *
 * This is a synchronous X round trip (tens of microseconds), so actions that must act on the
 * truth - the maximize toggle, the close path, the geometry saver - call it directly instead
 * of trusting a cached value. Anything that renders reads [LocalWindowIsFullscreen].
 */
fun isFullscreenNow(window: Window): Boolean {
    if (!Platform.isLinux()) return false
    val xid = window.xidOrNull() ?: return false
    return X11Ewmh.isFullscreen(xid)
}

/**
 * Leaves the window manager fullscreen state, if any. Asynchronous - kwin un-fullscreens on
 * its next pass and the window reports its restored floating geometry through the usual AWT
 * events, which compose picks up on its own.
 */
fun exitFullscreen(window: Window) {
    if (!Platform.isLinux()) return
    val xid = window.xidOrNull() ?: return
    X11Ewmh.setFullscreen(xid, false)
}

/**
 * Called only when the window is actually being disposed, never on a potentially vetoed
 * close request. OpenJDK's XFramePeer.setVisible(false) removes the maximized bits BEFORE
 * unmapping the frame, which starts KWin's restore animation. Withdraw first so the WM's
 * normal close animation uses the maximized/fullscreen geometry, not the restored one.
 * Do not set a skip-close-animation flag: the close effect itself is wanted.
 * The final sync orders the unmap ahead of AWT's X connection. Floating windows and
 * other platforms are untouched.
 */
fun prepareForWindowDisposal(window: Window) {
    if (!Platform.isLinux() || !window.isDisplayable || !window.isVisible) return
    if (!window.isMaximizedNow() && !isFullscreenNow(window)) return
    val xid = window.xidOrNull() ?: return
    if (X11Ewmh.unmap(xid)) {
        X11Ewmh.sync()
    }
}

/**
 * Whether a title bar drag can be handed to the window manager: linux with an X11 session
 * whose window manager honors `_NET_WM_MOVERESIZE`. Session level, the native handle is
 * checked again when the drag actually runs.
 */
fun canEwmhMove(): Boolean {
    if (!Platform.isLinux()) return false
    return X11Ewmh.supportsMoveResize()
}

/**
 * Hands the in-progress drag to the window manager - what a native title bar drag does. KWin
 * un-maximizes (or un-tiles) into the gesture and owns it until the button is released.
 * Call only while the drag button is held, with the pointer over the window.
 */
fun nativeMoveNow(window: Window) {
    if (!Platform.isLinux()) return
    val xid = window.xidOrNull() ?: return
    val pointer = currentPointerPosition()
    X11Ewmh.sendMoveResize(xid, pointer.x, pointer.y)
}

/**
 * Drag continuation for a fullscreen window: the manager ignores moves of fullscreen windows,
 * so the fullscreen is left first and the drag handed over once the window is back. Aborts
 * silently - the window just stays where it is - if the button goes up meanwhile or the
 * un-fullscreen takes too long.
 *
 * Must be called with the drag button held; [this] observes the gesture to notice the release.
 */
suspend fun AwaitPointerEventScope.awaitUnfullscreenAndNativeMove(window: Window) {
    exitFullscreen(window)
    val deadline = System.currentTimeMillis() + 500
    while (System.currentTimeMillis() < deadline) {
        if (!isFullscreenNow(window)) {
            nativeMoveNow(window)
            return
        }
        // wait a beat, aborting early if the drag ended meanwhile
        val event = withTimeoutOrNull(25) { awaitPointerEvent(PointerEventPass.Main) }
        if (event != null && !event.buttons.isPrimaryPressed) return
    }
}

private fun currentPointerPosition(): IntOffset {
    val location = runCatching { MouseInfo.getPointerInfo()?.location }.getOrNull()
    return if (location != null) IntOffset(location.x, location.y) else IntOffset.Zero
}

/**
 * The X11 window id, or `null` when it cannot be known (not a compose window, not realized
 * yet, undiscoverable render path). Compose exposes the handle of the root window, which on
 * X11 is exactly the window that carries `_NET_WM_STATE`.
 */
private fun Window.xidOrNull(): Long? {
    val window = this as? ComposeWindow ?: return null
    return runCatching { window.windowHandle }.getOrNull()?.takeIf { it != 0L }
}

/**
 * Any maximized bits of the native window, full or half (tiled). Unlike compose's placement
 * this is live, with no sync round trip behind the native window.
 */
fun Window.isMaximizedNow(): Boolean {
    val frame = this as? Frame ?: return false
    return (frame.extendedState and Frame.MAXIMIZED_BOTH) != 0
}

/**
 * Tracks the native window state that compose either does not see at all (window manager
 * fullscreen, invisible to AWT) or only sees after a sync round trip (the maximized bits).
 *
 * Fullscreen always resizes the window, and maximize always fires a window state event, so
 * re-reading on those AWT events keeps this fresh without polling or a second X event loop.
 * Off linux the fullscreen part just stays `false`, the maximized bits are plain AWT and track
 * everywhere.
 */
class LinuxWindowStateTracker(private val window: Window) {
    private val _isFullscreen = MutableStateFlow(isFullscreenNow(window))
    val isFullscreen: StateFlow<Boolean> = _isFullscreen.asStateFlow()

    private val _extendedState = MutableStateFlow((window as? Frame)?.extendedState ?: 0)
    val extendedState: StateFlow<Int> = _extendedState.asStateFlow()

    fun refresh() {
        val extendedState = (window as? Frame)?.extendedState ?: 0
        if (_extendedState.value != extendedState) {
            _extendedState.value = extendedState
        }
        if (!Platform.isLinux()) return
        val fullscreen = isFullscreenNow(window)
        if (_isFullscreen.value != fullscreen) {
            _isFullscreen.value = fullscreen
        }
    }
}

@Composable
fun FrameWindowScope.rememberLinuxWindowStateTracker(): LinuxWindowStateTracker {
    val tracker = remember(window) { LinuxWindowStateTracker(window) }
    DisposableEffect(window) {
        val componentListener = object : ComponentAdapter() {
            override fun componentResized(e: ComponentEvent) = tracker.refresh()
            override fun componentMoved(e: ComponentEvent) = tracker.refresh()
        }
        val windowStateListener = WindowStateListener { tracker.refresh() }
        window.addComponentListener(componentListener)
        window.addWindowStateListener(windowStateListener)
        tracker.refresh()
        onDispose {
            window.removeComponentListener(componentListener)
            window.removeWindowStateListener(windowStateListener)
        }
    }
    return tracker
}

/**
 * Whether the window manager shows the current window fullscreen. `false` everywhere but
 * linux, and `false` when the state cannot be known.
 */
val LocalWindowIsFullscreen = compositionLocalOf { false }
