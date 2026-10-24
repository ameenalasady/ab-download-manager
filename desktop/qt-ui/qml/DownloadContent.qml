pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Abdm.Desktop

// One download's live view. While it runs this is a compact progress page: file icon
// with size, name, status, progress bar, the transfer numbers and the engine's parts.
// Once it completes the same content becomes the original Compose completion page:
// the file icon and size, a success heading, the name, and the Open actions.
// Reusable inside a download toplevel OR the browser review's existing toplevel.
//
// Deliberate removals: the "Parts" toggle (parts are always listed, up to eight rows
// before scrolling), the "Parts (N)" heading, the separate Name/Size info rows (both
// moved into the header) and the folder-mode completedFileUrl() call (showInFolder()
// selects the file inside its folder itself). Remaining objectNames are unchanged.
Item {
    id: pane
    objectName: "downloadContent"
    required property var service
    required property string downloadId
    signal closeRequested()
    // The host is easing its size: no scroll bar flashes in and out of the moving edge.
    property bool resizing: false
    readonly property string windowTitle: row.state === "downloading" && row.progress >= 0
        ? qsTr("%1% · %2").arg(Math.floor(row.progress * 100)).arg(lastName) : lastName || qsTr("Download")
    PreviewTheme { id: theme }
    property var row: ({})
    property var parts: ({})
    property bool partsWatched: false
    property string lastName: ""
    readonly property bool available: row.available === true
    readonly property string transferState: available ? row.state : ""
    readonly property bool completed: transferState === "completed"
    // The completion summary is compact; progress needs room for the part table.
    readonly property int preferredWidth: completed ? Tokens.completionWindowWidth : Tokens.downloadWindowWidth
    readonly property bool connected: service.connectionState === "ready"
    readonly property bool controlsEnabled: available && connected && !service.busy
    readonly property var reason: row.error || null
    readonly property bool ownError: service.errorMessage !== "" && service.errorTarget === downloadId
    property url fileUrl: ""
    // How many engine part rows are shown before the part list scrolls.
    readonly property int visiblePartRows: Tokens.visiblePartRows
    // Without the read-only parts capability there is nothing to show and no useful
    // message: the section stays hidden instead of reporting a misleading window limit.
    readonly property bool partsSupported: (service.capabilities || []).indexOf("download.parts") >= 0
    // Height of the visible content (progress page or completion summary). Both toplevels
    // size themselves from this instead of guessing a fixed height, and re-measure on
    // state transitions. The progress column's leading and trailing spacers already
    // carry the page padding, so nothing is added on top of the column's implicit height.
    // Zero until the scrolled column has a real width: a not-yet-laid-out column would
    // report a nonsensical height from text wrapped at zero width.
    readonly property DownloadCompletion completionContent: completion.item as DownloadCompletion
    readonly property real contentHeight: completed ? (completionContent ? completionContent.implicitHeight : 0)
        : contentColumn.width > 0 ? contentColumn.implicitHeight + actions.implicitHeight : 0
    // Layouts only calculate their implicit sizes while they are polished, and a hidden
    // window is never polished by the render loop. Polishing the tree by hand (children
    // first) lets a host adopt the real content size before its first frame; the result
    // is returned so the host does not have to read a stale binding.
    function ensureMeasured(): real {
        polishTree(pane)
        return contentHeight
    }
    function polishTree(item: Item): void {
        const children = item.children
        for (let index = 0; index < children.length; ++index) polishTree(children[index])
        if (item instanceof ColumnLayout || item instanceof RowLayout || item instanceof GridLayout
            || item instanceof Flow || item instanceof ListView || item instanceof QQC2.ScrollView)
            item.ensurePolished()
    }
    NumberAnimation { id: pageFade; property: "opacity"; from: 0; to: 1; duration: Tokens.pageTransitionMs; easing.type: Easing.OutCubic }
    property Item fadingPage: null
    function fadeIn(page: Item): void {
        const window = pane.Window.window
        if (!window || !window.visible || Tokens.pageTransitionMs <= 0) return
        pageFade.stop()
        if (fadingPage) fadingPage.opacity = 1
        fadingPage = page
        pageFade.target = page
        pageFade.restart()
    }
    function refresh() {
        row = service.downloadState(downloadId); parts = service.downloadParts(downloadId)
        partSlots.update(parts, service.settings.threadCount || 8)
        if (row.available) lastName = row.name
        fileUrl = service.completedFileUrl(downloadId, false)
    }
    DownloadPartSlots { id: partSlots }
    Component.onCompleted: { partsWatched = service.watchDownloadParts(downloadId, true); refresh() }
    Component.onDestruction: if (partsWatched) service.watchDownloadParts(downloadId, false)
    Connections { target: pane.service; function onViewChanged() { pane.refresh() } }
    function statusText(): string {
        if (!available) return qsTr("No longer in the download list")
        return ({downloading: qsTr("Downloading"), paused: qsTr("Paused"), completed: qsTr("Download complete"),
            failed: reason ? reason.title : qsTr("Download failed")})[transferState]
            || (row.waiting ? qsTr("Waiting for a free download slot") : qsTr("Not started"))
    }
    function progressText(): string {
        if (row.progress < 0) return qsTr("%1 (total size unknown)").arg(row.downloadedLabel)
        return qsTr("%1 of %2 (%3%)").arg(row.downloadedLabel).arg(row.sizeLabel).arg(Math.floor(row.progress * 100))
    }
    function partState(state: string): string {
        return ({idle: qsTr("Idle"), connecting: qsTr("Connecting"), downloading: qsTr("Downloading"),
            completed: qsTr("Finished"), disconnected: qsTr("Disconnected")})[state] || state
    }
    function partColor(state: string): color {
        return state === "downloading" || state === "completed" ? theme.positive : state === "disconnected" ? theme.error : theme.warning
    }
    component FieldLabel: QQC2.Label { Layout.alignment: Qt.AlignLeft | Qt.AlignTop; color: theme.secondary; textFormat: Text.PlainText }
    component FieldValue: QQC2.Label { Layout.fillWidth: true; textFormat: Text.PlainText; elide: Text.ElideMiddle; Accessible.name: text }
    component Body: TextBlock { colors: theme }
    component PartCell: QQC2.Label { textFormat: Text.PlainText; elide: Text.ElideRight }
    // Parts table columns: index, state (fills), downloaded, total.
    readonly property int partIndexWidth: Tokens.iconRow + Tokens.spacingM
    readonly property int partBytesWidth: Tokens.partBytesWidth
    readonly property int partTotalWidth: Tokens.partTotalWidth
    Loader {
        id: completion; anchors.fill: parent; active: pane.completed
        // A page that appears in an already shown window fades in while the size eases.
        onLoaded: pane.fadeIn(completion)
        sourceComponent: Component {
            DownloadCompletion { row: pane.row; fileUrl: pane.fileUrl; onCloseRequested: pane.closeRequested() }
        }
    }
    QQC2.ScrollView {
        id: scroller; contentWidth: availableWidth; clip: true; visible: !pane.completed
        onVisibleChanged: if (visible) pane.fadeIn(scroller)
        QQC2.ScrollBar.vertical.policy: pane.resizing ? QQC2.ScrollBar.AlwaysOff : QQC2.ScrollBar.AsNeeded
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: actions.top }
        ColumnLayout {
            id: contentColumn; x: Tokens.dialogPadding
            width: Math.max(0, scroller.availableWidth - 2 * Tokens.dialogPadding)
            spacing: Tokens.contentSpacing
            Item { implicitHeight: Tokens.dialogPadding - Tokens.contentSpacing; Layout.fillWidth: true }
            // Identity: file icon and size (as in the original completion page), then the
            // name and the state. The check mark only appears once the download finished.
            RowLayout {
                id: header; Layout.fillWidth: true; spacing: Tokens.spacingM
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop; spacing: Tokens.spacingXS
                    Kirigami.Icon {
                        implicitWidth: Tokens.iconLarge; implicitHeight: Tokens.iconLarge
                        source: pane.row.iconName || "text-x-generic"
                        Accessible.ignored: true
                    }
                    QQC2.Label {
                        Layout.alignment: Qt.AlignHCenter; textFormat: Text.PlainText
                        text: pane.row.sizeLabel || qsTr("Unknown"); color: theme.secondary; font: Tokens.smallFont
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Tokens.spacingXS
                    QQC2.Label {
                        objectName: "downloadWindowName"; Layout.fillWidth: true
                        text: pane.lastName || pane.downloadId; textFormat: Text.PlainText; elide: Text.ElideMiddle
                        font.weight: Font.DemiBold; Accessible.name: text
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: Tokens.spacingXS
                        Kirigami.Icon {
                            objectName: "downloadWindowCompletedIcon"; visible: pane.completed
                            implicitWidth: Tokens.iconRow; implicitHeight: implicitWidth
                            source: "checkmark"; color: theme.positive; Accessible.ignored: true
                        }
                        QQC2.Label {
                            objectName: "downloadWindowStatus"; Layout.fillWidth: true
                            text: pane.statusText(); textFormat: Text.PlainText; wrapMode: Text.Wrap
                            color: pane.completed ? theme.positive : pane.transferState === "failed" ? theme.error : theme.secondary
                            font.weight: pane.completed ? Font.DemiBold : Font.Normal
                            Accessible.name: text
                        }
                    }
                }
            }
            // One bar for both modes: a finished transfer shows it full in the success
            // color, which is the completion page's only "progress" echo.
            ProgressTrack {
                objectName: "downloadWindowProgress"; colors: theme; Layout.fillWidth: true
                value: pane.completed ? 1 : pane.row.progress >= 0 ? pane.row.progress : -1
                barColor: pane.transferState === "failed" ? theme.error : pane.completed ? theme.positive : theme.accent
                indeterminate: !pane.completed && pane.transferState === "downloading" && pane.Window.window !== null && pane.Window.window.visible
                Accessible.role: Accessible.ProgressBar; Accessible.name: qsTr("Download progress")
                Accessible.description: pane.completed ? qsTr("Complete") : known ? qsTr("%1 percent").arg(Math.floor(pane.row.progress * 100)) : qsTr("Total size unknown")
            }
            GridLayout {
                objectName: "downloadWindowInfo"; Layout.fillWidth: true; visible: !pane.completed
                columns: 2; columnSpacing: Tokens.formColumnSpacing; rowSpacing: Tokens.spacingS
                FieldLabel { text: qsTr("Downloaded:") }
                FieldValue { objectName: "downloadWindowBytes"; text: pane.available ? pane.progressText() : "—" }
                FieldLabel { text: qsTr("Speed:") }
                FieldValue { objectName: "downloadWindowSpeed"; text: pane.transferState === "downloading" ? pane.row.speedLabel || qsTr("Measuring…") : "—" }
                FieldLabel { text: qsTr("Time left:") }
                FieldValue { objectName: "downloadWindowEta"; text: pane.transferState === "downloading" ? pane.row.etaLabel || (pane.row.progress < 0 ? qsTr("Unknown") : qsTr("Measuring…")) : "—" }
                FieldLabel { text: qsTr("Resume support:") }
                FieldValue { text: pane.parts.resumeSupport === true ? qsTr("Yes") : pane.parts.resumeSupport === false ? qsTr("No") : qsTr("Unknown") }
                FieldLabel { text: qsTr("Location:"); visible: (pane.row.folder || "") !== "" }
                FieldValue { objectName: "downloadWindowFolder"; text: pane.row.folder || ""; visible: text !== "" }
                FieldLabel { text: qsTr("Source:"); visible: (pane.row.url || "") !== "" }
                FieldValue { text: pane.row.url || ""; visible: text !== "" }
            }
            StatusMessage {
                type: Kirigami.MessageType.Warning; visible: !pane.completed && pane.parts.resumeSupport === false
                message: qsTr("This server can't resume downloads. Pausing will restart it.")
            }
            // Engine parts, always shown while the transfer is not finished. No heading:
            // the piece bar and the column captions already identify the table.
            ColumnLayout {
                objectName: "downloadWindowParts"; Layout.fillWidth: true
                visible: !pane.completed && pane.partsSupported
                spacing: Tokens.spacingM
                Rectangle {
                    id: pieces; objectName: "downloadWindowPieces"; Layout.fillWidth: true; implicitHeight: Tokens.progressHeight; color: theme.border; clip: true
                    visible: pane.parts.available === true && Number(pane.row.size) > 0
                    Repeater {
                        model: pane.parts.items || []
                        delegate: Rectangle {
                            required property var modelData
                            x: pieces.width * Number(modelData.offset) / Number(pane.row.size)
                            width: modelData.length === null ? 0 : pieces.width * Number(modelData.length) / Number(pane.row.size)
                            height: pieces.height; color: "transparent"
                            Rectangle { width: parent.width * Math.max(0, parent.modelData.progress); height: parent.height; color: pane.partColor(parent.modelData.state) }
                        }
                    }
                    Accessible.role: Accessible.ProgressBar; Accessible.name: qsTr("Engine download pieces")
                }
                Body {
                    tone: "secondary"
                    visible: !pane.partsWatched || pane.parts.available !== true
                    text: !pane.partsWatched ? qsTr("Too many detail windows are open. Close one to see parts.")
                        : qsTr("No engine part information available yet.")
                }
                StatusMessage { type: Kirigami.MessageType.Warning; visible: pane.parts.truncated === true; message: qsTr("Showing the first 256 parts.") }
                RowLayout {
                    Layout.fillWidth: true; Layout.leftMargin: Tokens.cellPadding; Layout.rightMargin: Tokens.cellPadding
                    spacing: Tokens.spacingS; visible: partList.count > 0
                    PartCell { Layout.preferredWidth: pane.partIndexWidth; text: "#"; color: theme.secondary; font.weight: Font.DemiBold }
                    PartCell { Layout.fillWidth: true; text: qsTr("Status"); color: theme.secondary; font.weight: Font.DemiBold }
                    PartCell { Layout.preferredWidth: pane.partBytesWidth; text: qsTr("Downloaded"); color: theme.secondary; font.weight: Font.DemiBold }
                    PartCell { Layout.preferredWidth: pane.partTotalWidth; text: qsTr("Total"); color: theme.secondary; font.weight: Font.DemiBold }
                }
                SurfaceFrame {
                    id: partFrame; colors: theme
                    // Eight rows are budgeted from the model rather than from the delegates
                    // that happen to exist: the pane must measure the same before its first
                    // frame (no delegates yet) as it does once scrolling is live.
                    Layout.fillWidth: true; Layout.preferredHeight: Math.min(partList.count, pane.visiblePartRows) * Tokens.rowHeightCompact + 2 * inset
                    visible: partList.count > 0
                    ListView {
                        id: partList; objectName: "downloadWindowPartList"
                        anchors.fill: parent; anchors.margins: Tokens.separatorWidth
                        anchors.topMargin: partFrame.inset; anchors.bottomMargin: partFrame.inset
                        // Incremental slot updates retain delegates and the user's scroll
                        // position. The raw range array resets a ListView on every poll.
                        model: partSlots; clip: true; boundsBehavior: Flickable.StopAtBounds
                        QQC2.ScrollBar.vertical: QQC2.ScrollBar {}
                        delegate: Rectangle {
                            id: part; required property var modelData; required property int index
                            width: ListView.view.width; height: Tokens.rowHeightCompact; color: index % 2 ? Qt.alpha(theme.alternate, Tokens.alternateOpacity) : "transparent"
                            Accessible.role: Accessible.ListItem
                            Accessible.name: qsTr("Part %1: %2, %3 of %4").arg(index + 1).arg(pane.partState(modelData.state)).arg(modelData.downloadedLabel).arg(modelData.sizeLabel)
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: Tokens.cellPadding; anchors.rightMargin: Tokens.cellPadding; spacing: Tokens.spacingS
                                PartCell { Layout.preferredWidth: pane.partIndexWidth; text: String(part.index + 1) }
                                PartCell { Layout.fillWidth: true; text: pane.partState(part.modelData.state) }
                                PartCell { Layout.preferredWidth: pane.partBytesWidth; text: part.modelData.downloadedLabel }
                                PartCell { Layout.preferredWidth: pane.partTotalWidth; text: part.modelData.sizeLabel }
                            }
                        }
                    }
                }
            }
            StatusMessage {
                objectName: "downloadWindowError"; type: Kirigami.MessageType.Error
                visible: pane.transferState === "failed" && pane.reason !== null
                message: pane.reason ? [pane.reason.description, pane.reason.suggestion].filter(t => t).join("\n\n") : ""
            }
            StatusMessage {
                type: Kirigami.MessageType.Warning; visible: pane.transferState === "failed" && pane.reason === null
                message: qsTr("The error reason is unavailable.")
            }
            StatusMessage {
                objectName: "downloadWindowCommandError"; type: Kirigami.MessageType.Error
                visible: pane.ownError; message: pane.service.errorMessage
            }
            StatusMessage {
                objectName: "downloadWindowStale"; type: Kirigami.MessageType.Warning
                visible: !pane.connected
                message: qsTr("Disconnected. Showing the last known state.")
            }
            StatusMessage {
                type: Kirigami.MessageType.Information
                visible: !pane.available && pane.connected
                message: qsTr("This download was removed.")
            }
            Item { implicitHeight: Tokens.dialogPadding - Tokens.contentSpacing; Layout.fillWidth: true }
        }
    }    // Same footer as the completion page and every dialog: separator, right-aligned,
    // wrapping at narrow widths. Close never pauses, cancels or deletes.
    WindowFooter {
        id: actions
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        visible: !pane.completed
        UiButton {
            objectName: "downloadWindowPause"; highlighted: true; text: qsTr("Pause"); icon.name: "media-playback-pause"
            visible: pane.transferState === "downloading" || (pane.transferState === "queued" && pane.row.waiting === true)
            enabled: pane.controlsEnabled; onClicked: pane.service.pauseDownload(pane.downloadId)
        }
        UiButton {
            objectName: "downloadWindowResume"; highlighted: true; icon.name: "media-playback-start"
            text: pane.transferState === "failed" ? qsTr("Retry") : pane.transferState === "paused" ? qsTr("Resume") : qsTr("Start")
            visible: pane.transferState === "paused" || pane.transferState === "failed" || (pane.transferState === "queued" && pane.row.waiting !== true)
            enabled: pane.controlsEnabled; onClicked: pane.service.resumeDownload(pane.downloadId)
        }
        UiButton {
            objectName: "downloadWindowClose"; text: qsTr("Close"); icon.name: "window-close"
            Accessible.description: qsTr("Closes this window; the download continues")
            onClicked: pane.closeRequested()
        }
    }
}
