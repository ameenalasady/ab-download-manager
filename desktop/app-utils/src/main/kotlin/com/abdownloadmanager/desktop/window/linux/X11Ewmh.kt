package com.abdownloadmanager.desktop.window.linux

import com.sun.jna.NativeLong
import com.sun.jna.platform.unix.X11
import com.sun.jna.ptr.IntByReference
import com.sun.jna.ptr.NativeLongByReference
import com.sun.jna.ptr.PointerByReference

/**
 * Minimal EWMH client for X11, used for the things AWT cannot do: see whether the window
 * manager has fullscreened one of our windows, ask for a native move, and withdraw a window
 * before AWT's disposal can restore it.
 *
 * AWT only tracks the iconic/maximized bits of `_NET_WM_STATE`, so when kwin (or mutter,
 * xfwm, ...) fullscreens a window behind our back, [java.awt.Frame.getExtendedState] keeps
 * reporting `NORMAL` and compose keeps reporting `Floating` - while the window is actually
 * covering the whole screen.
 *
 * Everything here fails soft: no X display, no EWMH atoms, a window that is not realized yet
 * or already disposed - all of that just reads as "not available" and every request becomes
 * a no-op, so callers never have to handle X11 being unavailable.
 */
internal object X11Ewmh {
    private const val NET_WM_STATE = "_NET_WM_STATE"
    private const val NET_WM_STATE_FULLSCREEN = "_NET_WM_STATE_FULLSCREEN"
    private const val NET_SUPPORTED = "_NET_SUPPORTED"
    private const val NET_WM_MOVERESIZE = "_NET_WM_MOVERESIZE"

    // _NET_WM_STATE client message actions, see the EWMH spec
    private const val STATE_REMOVE = 0L
    private const val STATE_ADD = 1L

    // _NET_WM_MOVERESIZE direction "move the window", the rest are resizes
    private const val MOVERESIZE_MOVE = 8L

    // source indication of a normal application request
    private const val SOURCE_APPLICATION = 1L

    private val lock = Any()
    private var displayTried = false
    private var display: X11.Display? = null
    private val atoms = mutableMapOf<String, X11.Atom>()
    private var moveResizeSupport: Boolean? = null

    private fun display(): X11.Display? = synchronized(lock) {
        if (!displayTried) {
            displayTried = true
            // a second connection next to AWT's own, used from the EDT only
            display = runCatching { X11.INSTANCE.XOpenDisplay(null) }.getOrNull()
        }
        display
    }

    private fun atom(name: String): X11.Atom? = synchronized(lock) {
        atoms[name]?.let { return it }
        val display = display() ?: return null
        val atom = runCatching { X11.INSTANCE.XInternAtom(display, name, false) }.getOrNull()
            ?: return null
        atoms[name] = atom
        atom
    }

    /**
     * Reads an `ATOM[]` (format 32) property.
     *
     * @return the atoms, or `null` when the property cannot be read
     */
    private fun atomArrayProperty(window: X11.Window, property: X11.Atom): LongArray? {
        val display = synchronized(lock) { display() } ?: return null
        val actualType = X11.AtomByReference()
        val actualFormat = IntByReference()
        val itemCount = NativeLongByReference()
        val bytesAfter = NativeLongByReference()
        val prop = PointerByReference()
        val status = runCatching {
            X11.INSTANCE.XGetWindowProperty(
                display,
                window,
                property,
                NativeLong(0),
                NativeLong(4096),
                false,
                X11.XA_ATOM,
                actualType,
                actualFormat,
                itemCount,
                bytesAfter,
                prop,
            )
        }.getOrNull() ?: return null
        // Success
        if (status != 0) return null
        val pointer = prop.value ?: return null
        return try {
            if (actualFormat.value != 32) return null
            val count = itemCount.value.toInt().coerceAtLeast(0)
            if (count == 0) return LongArray(0)
            pointer.getLongArray(0, count)
        } catch (_: Throwable) {
            null
        } finally {
            runCatching { X11.INSTANCE.XFree(pointer) }
        }
    }

    fun isFullscreen(xid: Long): Boolean {
        if (xid == 0L) return false
        val netWmState = atom(NET_WM_STATE) ?: return false
        val fullscreen = atom(NET_WM_STATE_FULLSCREEN) ?: return false
        return runCatching {
            atomArrayProperty(X11.Window(xid), netWmState)?.contains(fullscreen.toLong()) == true
        }.getOrDefault(false)
    }

    /**
     * Whether the window manager honors `_NET_WM_MOVERESIZE`, i.e. whether [sendMoveResize]
     * does anything. Read once, a session does not change its window manager.
     */
    fun supportsMoveResize(): Boolean {
        synchronized(lock) {
            moveResizeSupport?.let { return it }
        }
        val supported = runCatching {
            val display = synchronized(lock) { display() } ?: return false
            val netSupported = atom(NET_SUPPORTED) ?: return false
            val moveResize = atom(NET_WM_MOVERESIZE) ?: return false
            val root = X11.INSTANCE.XDefaultRootWindow(display)
            atomArrayProperty(root, netSupported)?.contains(moveResize.toLong()) == true
        }.getOrDefault(false)
        synchronized(lock) {
            moveResizeSupport = supported
        }
        return supported
    }

    /**
     * Asks the window manager to move [xid] with the pointer, exactly what a native title bar
     * drag does: kwin un-maximizes (or un-tiles) into the gesture and owns it until the button
     * is released. Must be called while the drag button is held, with the pointer over the
     * window - the caller guarantees that from the gesture that is in progress.
     *
     * @return whether the request was handed to the X server
     */
    fun sendMoveResize(xid: Long, xRoot: Int, yRoot: Int): Boolean {
        if (xid == 0L) return false
        val display = synchronized(lock) { display() } ?: return false
        val moveResize = atom(NET_WM_MOVERESIZE) ?: return false
        return runCatching {
            val root = X11.INSTANCE.XDefaultRootWindow(display)
            val event = newClientMessage(
                display = display,
                window = X11.Window(xid),
                messageType = moveResize,
                data = longArrayOf(
                    xRoot.toLong(),
                    yRoot.toLong(),
                    MOVERESIZE_MOVE,
                    1L, // Button1, the drag button the caller is tracking
                    SOURCE_APPLICATION,
                ),
            )
            val mask = NativeLong(
                (X11.SubstructureRedirectMask or X11.SubstructureNotifyMask).toLong()
            )
            val sent = X11.INSTANCE.XSendEvent(display, root, 0, mask, event)
            X11.INSTANCE.XFlush(display)
            sent != 0
        }.getOrDefault(false)
    }

    /**
     * Asks the window manager to add or remove the fullscreen state, the same request a
     * toolkit without a fullscreen API would send. This is asynchronous - kwin applies it on
     * its next pass, which resizes the window and makes AWT fire the usual events.
     *
     * @return whether the request was handed to the X server
     */
    fun setFullscreen(xid: Long, fullscreen: Boolean): Boolean {
        if (xid == 0L) return false
        val display = synchronized(lock) { display() } ?: return false
        val netWmState = atom(NET_WM_STATE) ?: return false
        val netWmStateFullscreen = atom(NET_WM_STATE_FULLSCREEN) ?: return false
        return runCatching {
            val root = X11.INSTANCE.XDefaultRootWindow(display)
            val event = newClientMessage(
                display = display,
                window = X11.Window(xid),
                messageType = netWmState,
                data = longArrayOf(
                    if (fullscreen) STATE_ADD else STATE_REMOVE,
                    netWmStateFullscreen.toLong(),
                    0L,
                    SOURCE_APPLICATION,
                    0L,
                ),
            )
            val mask = NativeLong(
                (X11.SubstructureRedirectMask or X11.SubstructureNotifyMask).toLong()
            )
            val sent = X11.INSTANCE.XSendEvent(display, root, 0, mask, event)
            X11.INSTANCE.XFlush(display)
            sent != 0
        }.getOrDefault(false)
    }

    /**
     * Withdraws the native window before AWT disposes its peer. XFramePeer.setVisible(false)
     * sends a restore request before unmapping a maximized frame; doing the unmap first lets
     * the WM release it before that request can start a maximize/restore animation.
     */
    fun unmap(xid: Long): Boolean {
        if (xid == 0L) return false
        val display = synchronized(lock) { display() } ?: return false
        return runCatching {
            X11.INSTANCE.XUnmapWindow(display, X11.Window(xid))
            X11.INSTANCE.XFlush(display)
            true
        }.getOrDefault(false)
    }

    /**
     * Round trip: when this returns, the X server has processed every request sent so far,
     * which orders our requests against anything another connection (AWT's) sends afterwards.
     */
    fun sync() {
        val display = synchronized(lock) { display() } ?: return
        runCatching { X11.INSTANCE.XSync(display, false) }
    }

    private fun newClientMessage(
        display: X11.Display,
        window: X11.Window,
        messageType: X11.Atom,
        data: LongArray,
    ): X11.XEvent {
        require(data.size == 5)
        val event = X11.XEvent()
        event.xclient.type = X11.ClientMessage
        event.xclient.serial = NativeLong(0)
        event.xclient.send_event = 1
        event.xclient.display = display
        event.xclient.window = window
        event.xclient.message_type = messageType
        event.xclient.format = 32
        event.xclient.data.l = Array(data.size) { NativeLong(data[it]) }
        event.xclient.data.setType(Array<NativeLong>::class.java)
        event.setType(X11.XClientMessageEvent::class.java)
        // nested structures share no memory with their parent, make sure each level is
        // flushed even if a future JNA stops cascading writes into unions
        event.xclient.data.write()
        event.xclient.write()
        event.write()
        return event
    }
}
