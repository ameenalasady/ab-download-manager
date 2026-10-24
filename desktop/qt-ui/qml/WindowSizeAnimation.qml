import QtQuick

// Eases a floating toplevel from its current size to the size a re-fit just chose. The
// compositor does not animate client-driven resizes, so this steps the size per frame
// (top-left anchored, like KWin's own resize). `fit(apply)` wraps the host's existing
// synchronous measure-and-set code: the target is read back from the window, the window
// is put back at its starting size in the same turn (no frame shows the target) and the
// animation runs between the two. Hosts relax their min/max size pins while `active`.
// Hidden, maximized, fullscreen windows and a zero animation speed apply instantly.
ParallelAnimation {
    id: root
    required property Window window
    property bool active: false
    property int targetWidth: 0
    property int targetHeight: 0

    NumberAnimation { target: root.window; property: "width"; to: root.targetWidth; duration: Tokens.pageTransitionMs; easing.type: Easing.OutCubic }
    NumberAnimation { target: root.window; property: "height"; to: root.targetHeight; duration: Tokens.pageTransitionMs; easing.type: Easing.OutCubic }
    // Retargeting stops the running ease; `active` must stay set across that, or the
    // host's size pins return for a moment and clamp the window.
    property bool retargeting: false
    onStopped: if (!retargeting) active = false

    function fit(apply: var): void {
        if (!window.visible || window.visibility !== Window.Windowed || Tokens.pageTransitionMs <= 0) {
            if (running) stop()
            apply()
            return
        }
        const wasRunning = running
        if (wasRunning) pause()
        const fromWidth = window.width, fromHeight = window.height
        // A re-fit that only touches the height must keep the width the ease is heading for.
        if (wasRunning) { window.width = targetWidth; window.height = targetHeight }
        active = true
        apply()
        const toWidth = window.width, toHeight = window.height
        window.width = fromWidth
        window.height = fromHeight
        // The same target (periodic re-fits) lets the running ease finish undisturbed.
        if (wasRunning && toWidth === targetWidth && toHeight === targetHeight) { resume(); return }
        if (wasRunning) {
            retargeting = true
            stop()
            retargeting = false
        }
        if (toWidth === fromWidth && toHeight === fromHeight) { active = false; return }
        targetWidth = toWidth
        targetHeight = toHeight
        active = true
        start()
    }
}
