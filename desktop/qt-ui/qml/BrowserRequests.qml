pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Abdm.Desktop

// Review of links handed off by the browser extension (non-silent requests). The
// service keeps captured cookies/headers; this view sees only URL, name and a flag.
Item {
    id: root
    required property var service
    // Independent native toplevel: main-window overlays never block browser intake.
    signal progressAccepted(var window, var ids)
    signal progressClosed(string id)
    signal existingRequested(string id)
    property var currentReview: null
    // Automatic progress windows the main view opens at once (DownloadsView.automaticWindowLimit).
    property int automaticWindowLimit: 3
    readonly property bool modalOpen: currentReview !== null && currentReview.visible && currentReview.downloadId === ""
    readonly property var pending: service.browserRequests || []
    readonly property bool explicitRuntime: service.runtime && service.runtime.mode === "explicit-runtime"
    PreviewTheme { id: theme }

    // Requests the user postponed with "Later": not reopened until a new request arrives.
    property var postponed: []
    function review(force: bool): var {
        if (currentReview && currentReview.visible && currentReview.downloadId === "") return
        const next = pending.find(r => force || postponed.indexOf(r.id) < 0)
        if (!next) return
        const window = reviewComponent.createObject(root)
        if (!window) return null
        currentReview = window
        window.load(next)
        window.activate("")
        return window
    }
    onPendingChanged: {
        postponed = postponed.filter(id => pending.some(r => r.id === id))
        if (currentReview && currentReview.downloadId === "") {
            const current = pending.find(r => r.id === currentReview.requestId)
            if (!current) currentReview.close()
            else currentReview.outcomeUnknown = current.outcomeUnknown === true
        }
        review(false)
    }
    Connections {
        target: root.service
        function onSelectionChanged() { if (root.currentReview && !root.service.busy) root.currentReview.submitting = false }
        function onBrowserMetadataReady(requestId: string, index: int, size: string, sizeLabel: string, state: string) {
            const window = root.currentReview
            if (!window || window.requestId !== requestId || !window.items[index]) return
            window.items[index].size = size; window.items[index].sizeLabel = sizeLabel; window.items[index].probeState = state
            window.nameRevision++
        }
        function onBrowserRequestDismissed(requestId: string) {
            if (root.currentReview && root.currentReview.requestId === requestId) root.currentReview.close()
        }
        function onBrowserRequestAccepted(requestId: string, ids: var, start: bool) {
            const window = root.currentReview
            if (!window || window.requestId !== requestId || window.downloadId !== "") return
            // Unselected links remain reviewable, but never reopen automatically after Add.
            if (root.postponed.indexOf(requestId) < 0) root.postponed = root.postponed.concat([requestId])
            if (start && (root.service.integration || {}).showDownloadProgressDialog !== false)
                root.progressAccepted(window, ids)
            if (window.downloadId === "") window.close()
            root.currentReview = null
        }
    }
    function reviewWindow(): var { return currentReview }
    Component {
      id: reviewComponent
      QQC2.ApplicationWindow {
        id: dialog; objectName: downloadId === "" ? "browserRequestDialog" : "downloadWindow-" + downloadId
        transientParent: null; flags: Qt.Window
        // Width is pinned during the first configure like height, so KWin's remembered
        // geometry from another app window cannot resize the review.
        // The pins are relaxed while a page change eases the size (WindowSizeAnimation),
        // or Qt would clamp the window to the new page at once.
        minimumWidth: initialConfigure && !sizeAnimation.active ? targetWidth() : Tokens.windowMinimumWidth
        maximumWidth: initialConfigure && !sizeAnimation.active ? targetWidth() : Tokens.windowMaximumHeight
        // Reviews and completion are genuinely content-sized. Advertising the generic
        // 260px progress minimum for a shorter review contradicts fitContentHeight();
        // when configureGuard releases maximumHeight, Qt clamps it up seconds later.
        minimumHeight: sizeAnimation.active ? Tokens.dialogPadding + Tokens.rowHeight
            : downloadId === "" || completedView
            ? Math.max(Tokens.dialogPadding + Tokens.rowHeight, Math.min(Math.ceil(measuredHeight()), heightLimit()))
            : Math.max(Tokens.windowMinimumHeight, Math.min(Math.ceil(measuredHeight()), heightLimit()))
        // Content-sized: the review starts as tall as its list, and the same native
        // surface is re-measured when it becomes a progress window or completes.
        property bool initialConfigure: true
        maximumHeight: initialConfigure && !sizeAnimation.active ? minimumHeight : Tokens.windowMaximumHeight
        WindowSizeAnimation { id: sizeAnimation; window: dialog }
        Timer { id: configureGuard; interval: Tokens.initialConfigureGuardMs; onTriggered: dialog.initialConfigure = false }
        width: Tokens.reviewWindowWidth; height: Tokens.reviewWindowHeight
        // The window is mapped only after its page has been measured, so the first frame
        // the compositor displays is already the natural size, not the review default.
        property bool fitting: false
        // Item-painted material, not a translucent clear color (see DownloadWindow).
        color: "transparent"
        background: Rectangle { color: material.background }
        NativeWindowStyle { id: material; window: dialog; maximumOpacity: Tokens.materialOpacity }
        function activate(token: string) { fitContentSize(); material.activate(token) }
        function open() { activate("") }
        readonly property bool opened: visible
        property string downloadId: ""
        property bool automatic: true
        readonly property DownloadContent progressContent: progress.item as DownloadContent
        readonly property var row: progressContent ? progressContent.row : ({})
        readonly property string transferState: progressContent ? progressContent.transferState : ""
        readonly property bool completedView: progressContent ? progressContent.completed : false
        property bool progressHydrationTimedOut: false
        readonly property bool progressReady: progressContent !== null && (progressHydrationTimedOut
            || !progressContent.available || progressContent.completed || !progressContent.connected
            || !progressContent.partsSupported || !progressContent.partsWatched
            || progressContent.parts.available !== undefined)
        onProgressReadyChanged: if (progressReady) fitContentSize()
        Timer {
            id: progressHydrationTimeout; interval: Tokens.windowHydrationTimeoutMs
            onTriggered: dialog.progressHydrationTimedOut = true
        }
        readonly property bool connected: root.service.connectionState === "ready"
        // Bounded content sizing (never a placement, move or new surface): measure what
        // is shown, clamp to the usable screen and re-measure a couple of times because
        // scroll bars and text wrapping change the measurement itself.
        function targetWidth(): int {
            if (downloadId !== "") return progressContent ? progressContent.preferredWidth : Tokens.downloadWindowWidth
            return items.length <= 1 ? Tokens.downloadWindowWidth : Tokens.reviewWindowWidth
        }
        function measuredHeight(): real {
            if (downloadId !== "" && progressReady) return progressContent.contentHeight
            return reviewColumn.implicitHeight + (reviewFooter.visible ? reviewFooter.implicitHeight : 0)
        }
        function heightLimit(): real {
            // Window.screen is QQuickScreenInfo: its usable height excludes panels/docks.
            const available = dialog.screen ? dialog.screen.desktopAvailableHeight - Tokens.dialogScreenMargin : 0
            return Math.max(Tokens.windowMinimumHeight, available > 0 ? available : Tokens.downloadWindowHeight)
        }
        // Layouts only calculate their implicit sizes while they are polished, and a hidden
        // window is never polished by the render loop; the list rows themselves only exist
        // after such a pass. Polishing the tree by hand (children first) is what makes the
        // hidden pre-fit possible.
        function polishTree(item: Item): void {
            const children = item.children
            for (let index = 0; index < children.length; ++index) polishTree(children[index])
            if (item instanceof ColumnLayout || item instanceof RowLayout || item instanceof GridLayout
                || item instanceof Flow || item instanceof ListView || item instanceof QQC2.ScrollView)
                item.ensurePolished()
        }
        function ensureMeasured(): void {
            if (downloadId !== "" && progressReady) {
                progressContent.ensureMeasured()
                return
            }
            polishTree(reviewColumn)
            polishTree(reviewFooter)
        }
        function fitContentSize() {
            if (fitting) return
            sizeAnimation.fit(applyContentSize)
        }
        function applyContentSize() {
            dialog.width = Math.max(Tokens.windowMinimumWidth, targetWidth())
            dialog.applyContentHeight()
        }
        function fitContentHeight() {
            if (fitting) return
            sizeAnimation.fit(applyContentHeight)
        }
        function applyContentHeight() {
            if (fitting) return
            fitting = true
            try {
                // Resolve width, wrapped labels and nested layouts while hidden. No
                // callLater/show race: activate() maps only after these passes finish.
                for (let pass = 0; pass < 6; ++pass) {
                    dialog.ensureMeasured()
                    const measured = Math.ceil(measuredHeight())
                    if (!(measured > 0)) break
                    // Content-sized on every page: a short single-link review has no filler gap.
                    // The progress page keeps its own minimum, which minimumHeight no longer
                    // advertises while the size eases.
                    const floor = Tokens.dialogPadding + Tokens.rowHeight
                    const pageFloor = downloadId === "" || completedView ? floor : Tokens.windowMinimumHeight
                    const target = Math.max(pageFloor, Math.min(measured, heightLimit()))
                    if (dialog.height === target) break
                    dialog.height = target
                }
            } finally { fitting = false }
        }
        // Page replacement reuses this native surface, fitted in the same turn.
        onProgressContentChanged: if (progressContent) dialog.fitContentSize()
        onDownloadIdChanged: if (downloadId !== "") {
            progressHydrationTimedOut = false
            progressHydrationTimeout.restart()
            dialog.fitContentSize()
            presentationFitTimer.restart()
        }
        onCompletedViewChanged: {
            dialog.ensureMeasured()
            dialog.fitContentSize()
            contentFitTimer.restart()
        }
        // Let the first compositor configure settle before re-fitting the mapped surface.
        Timer { id: presentationFitTimer; interval: 150; onTriggered: dialog.fitContentSize() }
        onVisibleChanged: if (visible) { initialConfigure = true; configureGuard.restart(); presentationFitTimer.restart() }
        Timer { id: contentFitTimer; interval: 0; onTriggered: dialog.fitContentHeight() }
        onHeightChanged: if (visible && height < minimumHeight && !fitting && !sizeAnimation.active) contentFitTimer.restart()
        onMinimumHeightChanged: if (visible && height < minimumHeight && !fitting && !sizeAnimation.active) contentFitTimer.restart()
        Connections { target: dialog.progressContent; function onContentHeightChanged() { contentFitTimer.restart() } }
        onClosing: {
            if (downloadId !== "") root.progressClosed(downloadId)
            else {
                if (root.postponed.indexOf(requestId) < 0) root.postponed = root.postponed.concat([requestId])
                if (root.currentReview === dialog) root.currentReview = null
                Qt.callLater(dialog.destroy)
            }
        }
        Shortcut { sequences: [StandardKey.Cancel]; onActivated: dialog.close() }
        property string requestId: ""
        property string requestSource: "browser"
        property bool outcomeUnknown: false
        property var items: []
        property var categoryChoices: []
        title: downloadId !== "" ? (progressContent ? progressContent.windowTitle : qsTr("Download"))
            : items.length > 1 ? qsTr("Add Multiple Downloads")
            : requestSource === "clipboard" ? qsTr("Review Download") : qsTr("Download from Browser")
        readonly property int checkedCount: items.filter(i => i.checked).length
        readonly property bool multiple: items.length > 1
        // A selected row's size check is still running in the service.
        readonly property bool probing: { nameRevision; return items.some(i => i.checked && i.probeState === "pending") }
        readonly property var filteredItems: {
            const query = reviewSearch.text.trim().toLowerCase()
            return items.filter(i => !query || i.name.toLowerCase().indexOf(query) >= 0 || i.url.toLowerCase().indexOf(query) >= 0)
        }
        function setAll(checked: bool) { items = items.map(i => Object.assign({}, i, {checked: checked})) }
        function fileIcon(name: string): string {
            const ext = name.split(".").pop().toLowerCase()
            if (["zip", "gz", "xz", "zst", "7z", "rar", "tar"].indexOf(ext) >= 0) return "package-x-generic"
            if (["exe", "msi", "appimage", "deb", "rpm"].indexOf(ext) >= 0) return "application-x-executable"
            if (["mp4", "webm", "mkv", "avi", "mov"].indexOf(ext) >= 0) return "video-x-generic"
            if (["mp3", "flac", "ogg", "wav", "m4a"].indexOf(ext) >= 0) return "audio-x-generic"
            if (["png", "jpg", "jpeg", "webp", "svg"].indexOf(ext) >= 0) return "image-x-generic"
            return "text-x-generic"
        }
        function load(request: var) {
            root.service.clearError()
            requestId = request.id; requestSource = request.source || "browser"
            outcomeUnknown = request.outcomeUnknown === true
            items = request.items.map((item, index) => ({index: index, url: item.url, name: item.name, hasHeaders: item.hasHeaders, checked: true,
                size: item.size || "-1", sizeLabel: item.sizeLabel || "—", probeState: item.probeState || ""}))
            reviewSearch.text = ""; inlinePolicy = "numbered"
            categoryChoices = [{id: "auto", name: qsTr("Automatic")}, {id: "none", name: qsTr("No Category")}]
                .concat((root.service.categories || []).map(c => ({id: c.id, name: c.name})))
            category.currentIndex = root.service.settings.useCategoryByDefault === false ? 1 : 0
            // Read-only outside the explicit runtime, but always shows where files will go.
            location.text = root.explicitRuntime && root.service.capabilities.indexOf("category.destination") >= 0 ? "" : (root.service.runtime || {}).destination || ""
            // Measure and map in one turn: the review is first shown at its natural size.
            dialog.fitContentSize()
        }
        // Existing downloads/files per link, known before Download is pressed. Names are
        // edited in place (keeping focus), so edits bump a revision to re-evaluate.
        property int nameRevision: 0
        function existingFor(item: var): var {
            const folder = location.text || (root.service.runtime || {}).destination || ""
            const matches = root.service.findDuplicates(item.url, item.name, folder)
            return {match: matches.length ? matches[0] : null, matches: matches, fileExists: root.service.destinationExists(folder, item.name)}
        }
        readonly property var existing: { nameRevision; return items.map(item => existingFor(item)) }
        readonly property var singleExisting: downloadId === "" && items.length === 1 && existing.length === 1 ? existing[0] : null
        // Replacing is offered for one link whose recorded download has the same destination
        // and is not transferring.
        function replaceable(matches: var, count: int): bool {
            return count === 1 && root.service.capabilities.indexOf("download.replace") >= 0
                && matches.some(m => m.sameDestination)
                && !matches.some(m => m.sameDestination && ["downloading", "queued"].indexOf(m.state) >= 0)
        }
        // A single link resolves duplicates inline: the banner's choice is the policy that
        // Download/Add Paused apply, so no second confirmation dialog is needed.
        readonly property bool singleDuplicate: Boolean(singleExisting && (singleExisting.match || singleExisting.fileExists))
        readonly property bool singleCanReplace: Boolean(singleExisting && replaceable(singleExisting.matches, 1))
        property string inlinePolicy: "numbered"
        function stateLabel(state: string): string {
            return ({downloading: qsTr("Downloading"), queued: qsTr("Queued"), paused: qsTr("Paused"),
                     completed: qsTr("Completed"), failed: qsTr("Failed")})[state] || ""
        }
        property var duplicateMatches: []
        property var pendingChosen: []
        property int pendingDuplicates: 0
        property bool pendingStart: false
        function acceptChosen(chosen: var, start: bool, policy: string) {
            if (root.service.acceptBrowserRequest(requestId, chosen, start, categoryChoices[category.currentIndex].id,
                                                   root.explicitRuntime ? location.text : "", policy)) submitting = true
        }
        function submit(start: bool) {
            const chosen = items.filter(i => i.checked).map(i => ({index: i.index, url: i.url, name: i.name}))
            const folder = location.text || (root.service.runtime || {}).destination || ""
            let matches = [], exists = false
            for (const item of chosen) {
                matches = matches.concat(root.service.findDuplicates(item.url, item.name, folder))
                exists = exists || root.service.destinationExists(folder, item.name)
            }
            const preference = root.service.settings.qtDuplicateAction || "ask"
            if (!matches.length && !exists) { acceptChosen(chosen, start, "numbered"); return }
            if (preference === "existing" && matches.length && chosen.length === 1 && items.length === 1) {
                root.existingRequested(matches[0].id)
                root.service.dismissBrowserRequest(requestId)
                return
            }
            if (items.length === 1) {
                acceptChosen(chosen, start, inlinePolicy === "overwrite" && replaceable(matches, chosen.length) ? "overwrite" : "numbered")
                return
            }
            if (preference === "numbered") { acceptChosen(chosen, start, "numbered"); return }
            duplicateMatches = matches; pendingChosen = chosen; pendingStart = start
            pendingDuplicates = chosen.filter(i => existing[i.index] && (existing[i.index].match || existing[i.index].fileExists)).length
            duplicates.open()
        }
        UiDialog {
            id: duplicates; objectName: "browserDuplicateDialog"
            title: qsTr("Some Links Already Exist"); preferredWidth: Tokens.dialogWidthMedium
            // Multi-link reviews only; rows already flag each duplicate, so this asks just
            // how to add them. A single link chooses inline in its banner instead.
            readonly property bool canReplace: dialog.replaceable(dialog.duplicateMatches, dialog.pendingChosen.length)
            contentItem: ColumnLayout {
                spacing: Tokens.contentSpacing
                TextBlock {
                    colors: theme
                    text: qsTr("%n of the selected links already exist in your downloads or download folder. Keep both to save numbered copies.", "", dialog.pendingDuplicates)
                }
                StatusMessage { type: Kirigami.MessageType.Error; message: root.service.errorMessage }
            }
            footer: DialogFooter {
                UiButton { id: browserDuplicateAdd; objectName: "browserDuplicateAddNew"; text: qsTr("Keep Both"); icon.name: "list-add"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole; onClicked: { dialog.acceptChosen(dialog.pendingChosen, dialog.pendingStart, "numbered"); if (dialog.submitting) duplicates.close() } }
                UiButton { objectName: "browserDuplicateOverwrite"; text: qsTr("Replace"); icon.name: "edit-delete"; visible: duplicates.canReplace; enabled: !root.service.busy; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.DestructiveRole; onClicked: { dialog.acceptChosen(dialog.pendingChosen, dialog.pendingStart, "overwrite"); if (dialog.submitting) duplicates.close() } }
                UiButton { text: qsTr("Cancel"); icon.name: "dialog-cancel"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: duplicates.reject() }
            }
        }
        property bool submitting: false
        Item {
          anchors.fill: parent
          Loader {
            id: progress; anchors.fill: parent; active: dialog.downloadId !== ""; visible: dialog.progressReady
            sourceComponent: Component { DownloadContent { resizing: sizeAnimation.active; service: root.service; downloadId: dialog.downloadId; onCloseRequested: dialog.close() } }
            onLoaded: presentationFitTimer.restart()
          }
          QQC2.ScrollView {
            id: reviewScroll; anchors.fill: parent; contentWidth: availableWidth; clip: true
            QQC2.ScrollBar.vertical.policy: sizeAnimation.active ? QQC2.ScrollBar.AlwaysOff : QQC2.ScrollBar.AsNeeded
            visible: dialog.downloadId === "" || !dialog.progressReady
            enabled: dialog.downloadId === ""
          ColumnLayout {
            id: reviewColumn
            x: Tokens.dialogPadding; width: reviewScroll.availableWidth - 2 * Tokens.dialogPadding
            spacing: Tokens.contentSpacing
            Item { implicitHeight: Tokens.dialogPadding - Tokens.contentSpacing; Layout.fillWidth: true }
            StatusMessage {
                objectName: "browserRequestUnknown"
                visible: dialog.outcomeUnknown; type: Kirigami.MessageType.Warning
                message: qsTr("Some of these links may already have been added. Check your downloads before discarding.")
            }
            RowLayout {
                Layout.fillWidth: true; visible: dialog.items.length > 1; spacing: Tokens.spacingS
                TextBlock {
                    objectName: "browserRequestSelection"; colors: theme; tone: "secondary"; wrapMode: Text.NoWrap
                    text: qsTr("%1 of %2 selected").arg(dialog.checkedCount).arg(dialog.items.length)
                        + (reviewSearch.text.trim() ? qsTr(" · %1 shown").arg(dialog.filteredItems.length) : "")
                }
                UiButton {
                    flat: true; text: dialog.checkedCount === dialog.items.length ? qsTr("Select None") : qsTr("Select All")
                    onClicked: dialog.setAll(dialog.checkedCount !== dialog.items.length)
                }
                Kirigami.SearchField {
                    id: reviewSearch; objectName: "browserRequestSearch"; Layout.fillWidth: true
                    placeholderText: qsTr("Search links…"); maximumLength: 256
                    Accessible.name: qsTr("Search links"); focusSequences: []
                }
            }
            // One framed View-colored list with contiguous rows, like Dolphin's details view.
            // Shows up to five rows before scrolling, measured from the rows themselves.
            SurfaceFrame {
                id: linkFrame; colors: theme
                readonly property real rowExtent: list.count > 0 ? Math.max(0, list.contentHeight - (dialog.multiple ? Tokens.rowHeight : 0)) / list.count : Tokens.rowHeightTall
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.preferredHeight: (dialog.multiple ? Tokens.rowHeight : 0) + Math.min(list.count * rowExtent, 5 * rowExtent) + 2 * inset
                Layout.minimumHeight: (dialog.multiple ? Tokens.rowHeight : 0) + rowExtent + 2 * inset
                QQC2.ScrollView {
                    anchors.fill: parent; anchors.margins: Tokens.separatorWidth
                    anchors.topMargin: linkFrame.inset; anchors.bottomMargin: linkFrame.inset
                    clip: true; contentWidth: dialog.multiple ? Math.max(availableWidth, Tokens.reviewTableMinimumWidth) : availableWidth
                    ListView {
                        id: list; objectName: "browserRequestItems"; model: dialog.filteredItems; spacing: 0; boundsBehavior: Flickable.StopAtBounds
                        headerPositioning: ListView.OverlayHeader
                        header: Rectangle {
                            z: 2; width: list.width; height: dialog.multiple ? Tokens.rowHeight : 0; visible: dialog.multiple
                            color: theme.surface
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: Tokens.spacingM; anchors.rightMargin: Tokens.spacingM; spacing: Tokens.spacingM
                                Item { Layout.preferredWidth: Tokens.rowHeight + Tokens.iconMedium + Tokens.spacingM }
                                QQC2.Label { Layout.preferredWidth: Tokens.reviewNameWidth; Layout.maximumWidth: Tokens.reviewNameWidth; text: qsTr("Name"); font.weight: Font.DemiBold; color: theme.secondary }
                                QQC2.Label { Layout.preferredWidth: Tokens.reviewSizeWidth; Layout.maximumWidth: Tokens.reviewSizeWidth; text: qsTr("Size"); font.weight: Font.DemiBold; color: theme.secondary }
                                QQC2.Label { Layout.fillWidth: true; text: qsTr("Link"); font.weight: Font.DemiBold; color: theme.secondary }
                                Item { visible: dialog.items.some(i => i.hasHeaders); Layout.preferredWidth: Tokens.iconSmall }
                            }
                            Kirigami.Separator { anchors.bottom: parent.bottom; width: parent.width }
                        }
                        Accessible.role: Accessible.List; Accessible.name: qsTr("Links")
                        delegate: Rectangle {
                            id: entry
                            required property var modelData
                            required property int index
                            width: ListView.view.width; implicitHeight: entryLayout.implicitHeight + 2 * Tokens.spacingS
                            color: index % 2 ? Qt.alpha(theme.alternate, Tokens.alternateOpacity) : "transparent"
                            RowLayout {
                                id: entryLayout
                                anchors.fill: parent; anchors.leftMargin: Tokens.spacingM; anchors.rightMargin: Tokens.spacingM
                                anchors.topMargin: Tokens.spacingS; anchors.bottomMargin: Tokens.spacingS
                                spacing: Tokens.spacingM
                                QQC2.CheckBox {
                                    objectName: "browserRequestChecked-" + entry.modelData.index
                                    checked: entry.modelData.checked
                                    visible: dialog.multiple; Layout.preferredWidth: Tokens.rowHeight
                                    Accessible.name: qsTr("Download %1").arg(entry.modelData.name)
                                    onToggled: { const next = dialog.items.slice(); next[entry.modelData.index] = Object.assign({}, next[entry.modelData.index], {checked: checked}); dialog.items = next }
                                }
                                Kirigami.Icon {
                                    implicitWidth: Tokens.iconMedium; implicitHeight: implicitWidth
                                    source: dialog.fileIcon(entry.modelData.name); Accessible.ignored: true
                                }
                                ColumnLayout {
                                    Layout.fillWidth: !dialog.multiple; spacing: Tokens.spacingXS
                                    Layout.preferredWidth: dialog.multiple ? Tokens.reviewNameWidth : -1
                                    Layout.maximumWidth: dialog.multiple ? Tokens.reviewNameWidth : Infinity
                                    FormField {
                                        objectName: "browserRequestName-" + entry.modelData.index
                                        Layout.fillWidth: true; text: entry.modelData.name; maximumLength: 180
                                        Accessible.name: qsTr("File name")
                                        // Edit in place without rebuilding the model (keeps focus/cursor).
                                        onTextEdited: { dialog.items[entry.modelData.index].name = text; dialog.nameRevision++ }
                                    }
                                    QQC2.Label {
                                        Layout.fillWidth: true; elide: Text.ElideMiddle; textFormat: Text.PlainText; color: theme.secondary
                                        font: Tokens.smallFont
                                        visible: !dialog.multiple
                                        text: entry.modelData.url; Accessible.name: qsTr("Link: %1").arg(entry.modelData.url)
                                    }
                                    RowLayout {
                                        readonly property var info: dialog.existing[entry.modelData.index] || null
                                        objectName: "browserRequestItemExisting"
                                        Layout.fillWidth: true; spacing: Tokens.spacingXS
                                        // A single link gets the fuller banner below instead.
                                        visible: dialog.items.length > 1 && Boolean(info && (info.match || info.fileExists))
                                        Kirigami.Icon { implicitWidth: Tokens.iconSmall; implicitHeight: implicitWidth; source: "data-warning"; Accessible.ignored: true }
                                        QQC2.Label {
                                            Layout.fillWidth: true; elide: Text.ElideRight; textFormat: Text.PlainText
                                            font: Tokens.smallFont; color: theme.warning
                                            text: parent.info && parent.info.match ? qsTr("Already in your downloads") : qsTr("A file with this name already exists")
                                        }
                                    }
                                }
                                QQC2.Label {
                                    objectName: "browserRequestSize-" + entry.modelData.index
                                    visible: dialog.multiple; Layout.preferredWidth: Tokens.reviewSizeWidth; Layout.maximumWidth: Tokens.reviewSizeWidth
                                    text: { dialog.nameRevision; return dialog.items[entry.modelData.index].sizeLabel }
                                    textFormat: Text.PlainText
                                    elide: Text.ElideRight; color: theme.secondary
                                }
                                QQC2.Label {
                                    visible: dialog.multiple; Layout.fillWidth: true
                                    text: entry.modelData.url; textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.secondary
                                    Accessible.name: qsTr("Link: %1").arg(entry.modelData.url)
                                    HoverHandler { id: linkHover }
                                    QQC2.ToolTip.visible: linkHover.hovered; QQC2.ToolTip.delay: Tokens.toolTipDelay; QQC2.ToolTip.text: text
                                }
                                // Fixed slot whenever any link has credentials, so name fields align.
                                Item {
                                    visible: dialog.items.some(i => i.hasHeaders)
                                    implicitWidth: Tokens.iconSmall; implicitHeight: implicitWidth
                                    Kirigami.Icon {
                                        objectName: "browserRequestCredentials"
                                        anchors.fill: parent; visible: entry.modelData.hasHeaders; source: "object-locked"
                                        Accessible.role: Accessible.StaticText; Accessible.name: qsTr("Uses captured cookies, headers or credentials")
                                        HoverHandler { id: lockHover }
                                        QQC2.ToolTip.visible: lockHover.hovered; QQC2.ToolTip.delay: Tokens.toolTipDelay
                                        QQC2.ToolTip.text: qsTr("Uses cookies or credentials captured from the browser.")
                                    }
                                }
                            }
                        }
                    }
                }
            }
            TextBlock { colors: theme; tone: "secondary"; visible: dialog.multiple && dialog.filteredItems.length === 0; text: qsTr("No matching links") }
            // Single-link review: what already exists, its actions, and how Download treats it.
            DuplicateNotice {
                id: existingNotice
                objectName: "browserRequestExisting"
                readonly property var match: dialog.singleExisting ? dialog.singleExisting.match : null
                readonly property bool showsExisting: (root.service.settings.qtDuplicateAction || "ask") === "existing" && Boolean(match)
                visible: dialog.singleDuplicate
                title: match ? qsTr("Already in your downloads") : qsTr("File already exists")
                detail: match ? (dialog.stateLabel(match.state) ? qsTr("%1 · %2").arg(match.name).arg(dialog.stateLabel(match.state)) : match.name)
                    : dialog.items.length ? qsTr("“%1” is already in the download folder.").arg(dialog.items[0].name) : ""
                hasExisting: Boolean(match)
                canOpen: Boolean(match && match.fileUrl.toString() !== "")
                canResume: Boolean(match && ["paused", "failed"].indexOf(match.state) >= 0 && !showsExisting)
                onShowExisting: root.existingRequested(match.id)
                onOpenFile: DesktopOpener.openFile(match.fileUrl, dialog)
                onResume: { if (!root.service.busy && root.service.resumeDownload(match.id)) dialog.close() }
                extraVisible: true
                RowLayout {
                    objectName: "browserRequestDuplicateChoice"
                    visible: dialog.singleCanReplace && !existingNotice.showsExisting
                    spacing: Tokens.spacingL
                    QQC2.RadioButton {
                        objectName: "browserRequestKeepBoth"
                        text: qsTr("Keep both"); checked: dialog.inlinePolicy !== "overwrite"
                        onToggled: if (checked) dialog.inlinePolicy = "numbered"
                        QQC2.ToolTip.visible: hovered; QQC2.ToolTip.delay: Tokens.toolTipDelay
                        QQC2.ToolTip.text: qsTr("Save the new download with a numbered name.")
                    }
                    QQC2.RadioButton {
                        objectName: "browserRequestReplace"
                        text: qsTr("Replace existing"); checked: dialog.inlinePolicy === "overwrite"
                        onToggled: if (checked) dialog.inlinePolicy = "overwrite"
                        QQC2.ToolTip.visible: hovered; QQC2.ToolTip.delay: Tokens.toolTipDelay
                        QQC2.ToolTip.text: qsTr("Remove the existing download and its file, then download again.")
                    }
                    Item { Layout.fillWidth: true }
                }
                QQC2.Label {
                    objectName: "browserRequestDuplicateHint"
                    Layout.fillWidth: true
                    visible: !dialog.singleCanReplace || existingNotice.showsExisting
                    text: existingNotice.showsExisting ? qsTr("Download will show the existing download instead.")
                        : qsTr("Downloading saves a numbered copy alongside it.")
                    textFormat: Text.PlainText; wrapMode: Text.Wrap
                    font: Tokens.smallFont; color: theme.secondary
                }
            }
            FormGrid {
                FormLabel { text: qsTr("Save in:") }
                RowLayout {
                    Layout.fillWidth: true; spacing: Tokens.spacingS
                FormField {
                    id: location; objectName: "browserRequestLocation"; Layout.fillWidth: true; readOnly: !root.explicitRuntime
                    placeholderText: root.service.capabilities.indexOf("category.destination") >= 0 ? qsTr("Category destination or default (optional override)") : ""
                    color: readOnly ? theme.secondary : theme.text
                    Accessible.name: qsTr("Download location")
                }
                FolderBrowseButton { objectName: "browserRequestLocationBrowse"; field: location; title: qsTr("Save in"); startFolder: (root.service.runtime || {}).destination || "" }
                }
                FormLabel { text: qsTr("Category:"); visible: category.visible }
                UiComboBox {
                    id: category; objectName: "browserRequestCategory"; Layout.fillWidth: true; model: dialog.categoryChoices; textRole: "name"
                    visible: root.service.capabilities.indexOf("category.create") >= 0; enabled: !root.service.busy; Accessible.name: qsTr("Download category")
                    onActivated: {
                        if (root.explicitRuntime && root.service.capabilities.indexOf("category.destination") >= 0) {
                            const c = root.service.categories.find(c => c.id === dialog.categoryChoices[currentIndex].id)
                            location.text = c && c.usePath ? c.path : ""
                        }
                    }
                }
            }
            // Copy audit (sizing pass): the bottom progress-window note was removed at the
            // requested scope. Its objectName stays as an inert placeholder for tooling
            // anchored to this page; it contributes no height.
            Item { objectName: "browserRequestWindowNote"; visible: false; implicitHeight: 0 }
            StatusMessage { type: Kirigami.MessageType.Error; message: root.service.errorMessage }
            Item { implicitHeight: Tokens.dialogPadding - Tokens.contentSpacing; Layout.fillWidth: true }
        }
          }
        }
        // Same separator, padding and right alignment as dialog footers; buttons wrap on
        // narrow windows. Collapses to zero height once the window shows progress.
        footer: Item {
            id: reviewFooter
            visible: dialog.downloadId === "" || !dialog.progressReady
            enabled: dialog.downloadId === ""
            implicitHeight: visible ? reviewActions.implicitHeight : 0
            WindowFooter {
              id: reviewActions
              anchors { left: parent.left; right: parent.right; top: parent.top }
              leading: [
                UiButton {
                  objectName: "probeBrowserRequestSizes"; flat: true; text: qsTr("Check Sizes")
                  visible: dialog.multiple && ["browser.probeBatch", "browser.probe"].some(c => root.service.capabilities.indexOf(c) >= 0)
                  enabled: !root.service.busy && !dialog.submitting && !dialog.outcomeUnknown && dialog.checkedCount > 0 && !dialog.probing
                  onClicked: root.service.probeBrowserItems(dialog.requestId, dialog.items.filter(i => i.checked).map(i => i.index))
                },
                UiButton {
                  objectName: "dismissBrowserRequest"; flat: true
                  text: qsTr("Discard"); icon.name: "edit-delete-remove"; enabled: !root.service.busy && !dialog.submitting
                  QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.DestructiveRole
                  onClicked: { if (root.service.dismissBrowserRequest(dialog.requestId)) dialog.submitting = true }
                }
              ]
            UiButton {
                objectName: "acceptBrowserRequest"; text: qsTr("Download"); icon.name: "download"; highlighted: true
                enabled: !root.service.busy && !dialog.submitting && !dialog.outcomeUnknown && dialog.checkedCount > 0; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
                onClicked: dialog.submit(true)
            }
            UiButton {
                objectName: "pauseBrowserRequest"; text: qsTr("Add Paused"); icon.name: "list-add"; enabled: !root.service.busy && !dialog.submitting && !dialog.outcomeUnknown && dialog.checkedCount > 0
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AlternateRole
                onClicked: dialog.submit(false)
            }
            UiButton {
                objectName: "laterBrowserRequest"; text: qsTr("Later"); icon.name: "chronometer"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole
                onClicked: { root.postponed = root.postponed.concat([dialog.requestId]); dialog.close() }
            }
            }
        }
      }
    }
}
