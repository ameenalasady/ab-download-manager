pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import Abdm.Desktop

// Standalone KWin-decorated toplevel. Measure hidden content and hydrate watched
// parts before mapping; the token height is only a budget, never the first frame.
QQC2.ApplicationWindow {
    id: downloadWindow
    required property var service
    required property string downloadId
    property bool automatic: false
    signal dismissed()
    objectName: "downloadWindow-" + downloadId
    transientParent: null
    flags: Qt.Window
    minimumWidth: Tokens.windowMinimumWidth
    // Usable screen height for a window of this role; also what a fit is clamped to.
    readonly property real availableHeight: screen
        ? Math.max(Tokens.windowMinimumHeight, screen.desktopAvailableHeight - Tokens.dialogScreenMargin)
        : Tokens.downloadWindowHeight
    // A late compositor configure must not collapse the freshly fitted page. The progress
    // view keeps the shared window minimum (eight part rows plus its sections); the
    // completion summary is genuinely content-sized and may be smaller than that minimum.
    readonly property int completionFloor: Tokens.dialogPadding + Tokens.rowHeight
    minimumHeight: content.completed
        ? Math.max(completionFloor, Math.min(Math.ceil(content.contentHeight), availableHeight))
        : Math.max(Tokens.windowMinimumHeight, Math.min(Math.ceil(content.contentHeight), availableHeight))
    // Advertise a tight initial native size range so KWin's remembered geometry
    // cannot produce an oversized first buffer. Normal height resizing is restored
    // after the bounded first-configure period; the content minimum remains.
    property bool initialConfigure: true
    maximumHeight: initialConfigure ? minimumHeight : Tokens.windowMaximumHeight
    Timer { id: configureGuard; interval: Tokens.initialConfigureGuardMs; onTriggered: downloadWindow.initialConfigure = false }
    width: Tokens.downloadWindowWidth; height: Tokens.downloadWindowHeight
    color: material.background
    title: content.windowTitle
    // Compatibility/read-only state for adapter assertions; all actions live in content.
    readonly property var row: content.row
    readonly property string transferState: content.transferState
    readonly property bool connected: content.connected
    NativeWindowStyle { id: material; window: downloadWindow; maximumOpacity: Tokens.materialOpacity }
    property bool fitWidthPending: true
    // A watched-parts response arrives on the next service poll. Keep the native
    // window hidden until that first response AND layout are ready; merely polishing
    // the initial empty cache still produces a short window that visibly expands.
    property bool presentationPending: false
    property bool presentationTimedOut: false
    property string pendingActivationToken: ""
    property int measuredPresentationHeight: 0
    property int stablePresentationPasses: 0
    readonly property bool waitingForParts: content.available && !content.completed
        && content.connected && content.partsSupported && content.partsWatched
        && content.parts.available === undefined
    function targetHeight(measured: real): real {
        const limit = Math.max(minimumHeight, availableHeight)
        if (!(measured > 0))
            return Math.max(minimumHeight, Math.min(Tokens.downloadWindowHeight, limit))
        return Math.max(minimumHeight, Math.min(Math.ceil(measured), limit))
    }
    // Adopt the content's own height. Measurement is forced on the item tree (layouts only
    // compute implicit sizes while polished), so it works while the window is still hidden.
    // A couple of passes are needed because the new height itself can add a scroll bar or
    // rewrap a label.
    function fitContent(): void {
        if (fitWidthPending) {
            width = Tokens.downloadWindowWidth
            fitWidthPending = false
        }
        for (let pass = 0; pass < 3; ++pass) {
            const measured = content.ensureMeasured()
            if (!(measured > 0)) return
            const target = targetHeight(measured)
            if (height === target) return
            height = target
        }
    }
    function present(): void {
        if (!presentationPending) return
        if (waitingForParts && !presentationTimedOut) {
            if (!hydrationTimeout.running) hydrationTimeout.start()
            return
        }
        fitContent()
        // Native style/deferred control creation can deliver font and padding
        // changes on the next event-loop turn. Require two stable hidden layout
        // passes, not just a synchronous implicitHeight reading.
        if (height === measuredPresentationHeight) ++stablePresentationPasses
        else { measuredPresentationHeight = height; stablePresentationPasses = 0 }
        if (stablePresentationPasses < 2) { presentationTimer.restart(); return }
        presentationPending = false
        hydrationTimeout.stop()
        material.activate(pendingActivationToken)
        pendingActivationToken = ""
    }
    // A broken/disconnected service must never leave a requested window hidden
    // indefinitely. Its measured unavailable/error content is a valid fallback.
    Timer {
        id: hydrationTimeout; interval: Tokens.windowHydrationTimeoutMs
        onTriggered: { downloadWindow.presentationTimedOut = true; downloadWindow.present() }
    }
    Timer { id: presentationTimer; interval: 16; onTriggered: downloadWindow.present() }
    Component.onCompleted: fitContent()
    // Fit only on presentation changes, never on polling progress updates or every resize.
    // The timer is the guard against a late KWin configure re-applying a stale size; the
    // immediate pass keeps a progress → completion transition from lingering at the old
    // height for a frame.
    Timer { id: fitTimer; interval: 150; onTriggered: downloadWindow.fitContent() }
    Timer { id: settleFitTimer; interval: 0; onTriggered: downloadWindow.fitContent() }
    // A page change is adopted after a short debounce rather than the full guard interval,
    // so engine data that arrives late (the part table, a warning) is not rendered clipped.
    Timer { id: contentFitTimer; interval: 16; onTriggered: downloadWindow.fitContent() }
    onVisibleChanged: if (visible) {
        initialConfigure = true
        configureGuard.restart()
        fitWidthPending = true
        fitTimer.restart()
    }
    onTransferStateChanged: {
        fitWidthPending = true
        downloadWindow.fitContent()
        settleFitTimer.restart()
        fitTimer.restart()
    }
    // Defer polish outside contentHeight/minimumHeight binding evaluation.
    onHeightChanged: if (visible && (height < minimumHeight || (initialConfigure && height !== minimumHeight))) {
        // Correct only the geometry here, without recursively polishing a binding.
        height = minimumHeight
        settleFitTimer.restart()
        fitTimer.restart()
    }
    onMinimumHeightChanged: if (visible && height < minimumHeight) { settleFitTimer.restart(); fitTimer.restart() }
    Connections {
        target: content
        function onPartsChanged() { if (downloadWindow.presentationPending) Qt.callLater(downloadWindow.present) }
        function onContentHeightChanged() {
            if (downloadWindow.presentationPending) Qt.callLater(downloadWindow.present)
            else { contentFitTimer.restart(); fitTimer.restart() }
        }
    }
    function activate(token: string) {
        if (visible) { material.activate(token); return }
        pendingActivationToken = token
        presentationPending = true
        presentationTimedOut = false
        measuredPresentationHeight = 0
        stablePresentationPasses = 0
        presentationTimer.restart()
    }
    onClosing: dismissed()
    Shortcut { sequences: [StandardKey.Cancel]; onActivated: downloadWindow.close() }
    DownloadContent {
        id: content; anchors.fill: parent; service: downloadWindow.service; downloadId: downloadWindow.downloadId
        onCloseRequested: downloadWindow.close()
    }
}
