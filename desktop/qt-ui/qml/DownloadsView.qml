pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtQml.Models
import org.kde.kirigami as Kirigami
import Abdm.Desktop

Item {
    id: root
    objectName: "downloadsPreview"
    required property var downloads
    property var addOptions: ({})
    readonly property bool live: downloads.liveMode
    readonly property bool explicitRuntime: live && downloads.runtime && downloads.runtime.mode === "explicit-runtime"
    readonly property bool addDialogVisible: addDialog.visible
    readonly property bool removeDialogVisible: removeDialog.visible
    readonly property bool actionsMenuVisible: actionsMenu.visible
    // Every dialog except the browser review, which waits for these to close.
    readonly property bool mainModalOpen: multipleAddDialog.visible || mutationDialog.visible || addDialog.visible || duplicateDialog.visible || removeDialog.visible || aboutDialog.visible || Boolean(workflows && workflows.modalOpen) || Boolean(categoryWorkflows && categoryWorkflows.modalOpen) || Boolean(detailsLoader.item && detailsLoader.item.modalOpen) || Boolean(httpOptionsLoader.item && httpOptionsLoader.item.modalOpen)
    readonly property bool modalOpen: mainModalOpen || Boolean(browserLoader.item && browserLoader.item.modalOpen)
    PreviewTheme { id: theme }
    NativeWindowStyle { id: material; window: root.Window.window; maximumOpacity: Tokens.materialOpacity }
    Rectangle { anchors.fill: parent; color: material.background; z: -1 }

    function fileCategory(name: string): string {
        const ext = name.split(".").pop().toLowerCase()
        if (["zip", "gz", "xz", "zst", "7z", "rar", "tar"].indexOf(ext) >= 0) return qsTr("Compressed")
        if (["exe", "msi", "appimage", "deb", "rpm"].indexOf(ext) >= 0) return qsTr("Programs")
        if (["mp4", "webm", "mkv", "avi", "mov"].indexOf(ext) >= 0) return qsTr("Videos")
        if (["mp3", "flac", "ogg", "wav", "m4a"].indexOf(ext) >= 0) return qsTr("Music")
        if (["png", "jpg", "jpeg", "webp", "svg"].indexOf(ext) >= 0) return qsTr("Pictures")
        return qsTr("Documents")
    }
    function categoryIcon(uri: string): string {
        if (["package-x-generic", "application-x-archive", "application-pdf", "video-x-generic", "audio-x-generic", "image-x-generic", "text-x-generic"].indexOf(uri) >= 0) return uri
        return ({"icon:pictureFile": "image-x-generic", "icon:zipFile": "package-x-generic", "icon:musicFile": "audio-x-generic",
                 "icon:folderOpen": "folder", "icon:fileOpen": "text-x-generic", "icon:videoFile": "video-x-generic",
                 "FileZip": "package-x-generic", "FileApplication": "application-x-executable", "FileVideo": "video-x-generic",
                 "FileMusic": "audio-x-generic", "FilePicture": "image-x-generic", "FileDocument": "text-x-generic"})[uri] || "text-x-generic"
    }
    readonly property var navigationCategories: {
        const all = {key: "all", label: qsTr("All Downloads"), icon: "folder-downloads"}
        const states = [{key: "completed", label: qsTr("Completed"), icon: "emblem-ok-symbolic"},
                        {key: "unfinished", label: qsTr("Unfinished"), icon: "view-refresh-symbolic"}]
        const categories = live ? downloads.categories.map(c => ({key: "category:" + c.id, label: c.name, icon: categoryIcon(c.icon)}))
            : [{key: "compressed", label: qsTr("Compressed"), icon: "package-x-generic"},
               {key: "programs", label: qsTr("Programs"), icon: "application-x-executable"},
               {key: "videos", label: qsTr("Videos"), icon: "video-x-generic"},
               {key: "music", label: qsTr("Music"), icon: "audio-x-generic"},
               {key: "pictures", label: qsTr("Pictures"), icon: "image-x-generic"},
               {key: "documents", label: qsTr("Documents"), icon: "text-x-generic"}]
        return [all].concat(states, categories)
    }
    function categoryLabel(id: string, name: string): string {
        if (!live) return fileCategory(name)
        for (let i = 0; i < downloads.categories.length; ++i)
            if (downloads.categories[i].items.indexOf(id) >= 0) return downloads.categories[i].name
        return qsTr("Uncategorized")
    }
    function sendDownload(start: bool, policy: string): bool {
        return live ? downloads.addConfiguredDownload(sourceInput.text, nameInput.text, start, addCategory.model[addCategory.currentIndex].id,
                        Object.assign({}, addOptions, {duplicatePolicy: policy}, explicitRuntime ? {folder: locationInput.text} : {}))
                    : downloads.addDownload(sourceInput.text, nameInput.text, start)
    }
    function submitDownload(start: bool): bool {
        if (!live) return sendDownload(start, "numbered")
        downloads.clearError()
        const folder = explicitRuntime ? locationInput.text : downloads.runtime.destination
        const matches = downloads.findDuplicates(sourceInput.text, nameInput.text, folder)
        const exists = downloads.destinationExists(folder, nameInput.text)
        if (!matches.length && !exists) return sendDownload(start, "numbered")
        const preference = downloads.settings.qtDuplicateAction || "ask"
        if (preference === "numbered") return sendDownload(start, "numbered")
        if (preference === "existing" && matches.length) {
            showDownloadWindow(matches[0].id, false)
            return true // Never silently resume or execute a file.
        }
        duplicateDialog.matches = matches; duplicateDialog.fileExists = exists
        duplicateDialog.start = start; duplicateDialog.open()
        return false
    }
    function activateDownload(id: string) {
        if (!live || !id) return
        const row = downloads.downloadState(id)
        const url = downloads.completedFileUrl(id, false)
        if (row.state === "completed" && url.toString() !== "") DesktopOpener.openFile(url, root.Window.window)
        else showDownloadWindow(id, false)
    }
    function selectRow(id: string, modifiers: int) {
        if (modifiers & Qt.ShiftModifier) downloads.selectRange(id, Boolean(modifiers & Qt.ControlModifier))
        else if (modifiers & Qt.ControlModifier) downloads.toggleSelected(id)
        else downloads.select(id)
        list.currentIndex = downloads.indexOf(id); list.forceActiveFocus()
    }
    readonly property bool singleSelection: downloads.selectionCount === 1
    readonly property var currentFileUrl: live && singleSelection ? downloads.completedFileUrl(downloads.selectedId, false) : ""
    readonly property var currentFolderUrl: live && singleSelection ? downloads.completedFileUrl(downloads.selectedId, true) : ""
    function connectionLabel(): string {
        return ({ready: qsTr("Connected"), connecting: qsTr("Connecting…"), disconnected: qsTr("Disconnected")})[downloads.connectionState] || downloads.connectionState
    }
    readonly property bool filtered: downloads.search !== "" || downloads.category !== "all"
    Connections {
        target: root.downloads
        function onViewChanged() { list.currentIndex = root.downloads.indexOf(root.downloads.selectedId) }
    }
    function stateLabel(state: string): string {
        return ({downloading: qsTr("Downloading"), queued: qsTr("Added"), paused: qsTr("Paused"), completed: qsTr("Completed"), failed: qsTr("Error")})[state] || state
    }
    function openAddDialog() {
        downloads.clearError(); addOptions = ({})
        locationInput.text = explicitRuntime ? downloads.runtime.destination : qsTr("Isolated profile / Downloads")
        sourceInput.text = live ? "" : "https://example.invalid/archive.zip"
        nameInput.text = live ? "" : "archive.zip"
        addDialog.categoryChoices = [{id: "auto", name: qsTr("Automatic")}, {id: "none", name: qsTr("No Category")}].concat(live ? downloads.categories.map(c => ({id: c.id, name: c.name})) : [])
        addCategory.currentIndex = 0; addDialog.open(); sourceInput.forceActiveFocus()
    }
    function openMultipleAdd() {
        if (!live || downloads.capabilities.indexOf("links.review") < 0) return
        downloads.clearError(); multipleLinks.text = ""; multipleAddDialog.open(); multipleLinks.forceActiveFocus()
    }
    function openMutation(operation: string) {
        if (!live || !singleSelection || !downloads.canMutateDownload(downloads.selectedId, operation)) return
        mutationDialog.operation = operation; mutationDialog.targetId = downloads.selectedId
        const row = downloads.downloadDetails(mutationDialog.targetId)
        mutationDialog.targetName = row.name; moveName.text = row.name; moveFolder.text = row.details.folder
        mutationDialog.open()
        if (operation === "download.move") moveName.forceActiveFocus()
    }
    function openRemoveDialog() {
        if (!downloads.selectedId || (live && !downloads.canRemove)) return
        removeDialog.targetId = downloads.selectedId; removeDialog.targetIds = downloads.selectedIds.slice()
        removeDialog.targetName = downloads.selectionCount > 1 ? qsTr("%1 downloads").arg(downloads.selectionCount) : downloads.selectedName
        removeDeleteFiles.checked = false
        removeDialog.open()
    }
    readonly property var workflows: workflowLoader.item
    function openSettings() { if (workflows) workflows.openSettings() }
    function openQueues(id: string) { if (workflows) workflows.openQueues(id) }
    readonly property var categoryWorkflows: categoryLoader.item
    function openCategories() { if (categoryWorkflows) categoryWorkflows.openCategories() }
    Loader {
        id: categoryLoader; active: root.live; anchors.fill: parent
        sourceComponent: Component { CategoryWorkflows { service: root.downloads } }
    }
    Loader {
        id: workflowLoader; active: root.live; anchors.fill: parent
        sourceComponent: Component { ServiceWorkflows { service: root.downloads } }
    }
    function openDetails() { if (detailsLoader.item) (detailsLoader.item as DownloadDetails).openDetails(downloads.selectedId) }
    Loader {
        id: detailsLoader; active: root.live; anchors.fill: parent
        sourceComponent: Component { DownloadDetails { service: root.downloads } }
    }
    Loader {
        id: httpOptionsLoader; active: root.live; anchors.fill: parent
        sourceComponent: Component { HttpOptions {
            service: root.downloads
            onOptionsAccepted: options => { root.addOptions = options }
        } }
    }
    Loader {
        id: browserLoader; active: root.live && root.downloads.capabilities.indexOf("browser.intake") >= 0; anchors.fill: parent
        sourceComponent: Component { BrowserRequests {
            service: root.downloads; automaticWindowLimit: root.automaticWindowLimit
            onProgressAccepted: (window, ids) => root.adoptReviewWindow(window, ids)
            onProgressClosed: id => Qt.callLater(root.closeDownloadWindow, id)
            onExistingRequested: id => root.showDownloadWindow(id, false)
        } }
    }
    function reviewBrowserRequests(): var { return activateBrowserReview("") }
    function activateBrowserReview(token: string): var {
        if (!browserLoader.item) return null
        const window = (browserLoader.item as BrowserRequests).reviewWindow() || (browserLoader.item as BrowserRequests).review(true)
        if (window) window.activate(token)
        return window
    }
    function adoptReviewWindow(window: var, ids: var) {
        // First selected ID reuses the *same* Wayland surface: screen, position and
        // activation context survive the transition. Others use remaining automatic
        // slots; excess IDs stay in the main list. No command or hint is replayed.
        for (let i = 0; i < ids.length; ++i) {
            const id = ids[i]
            if (!downloads.downloadState(id).available) continue
            if (i === 0 && !downloadWindows[id]
                && Object.keys(downloadWindows).filter(key => downloadWindows[key].automatic).length < automaticWindowLimit) {
                window.downloadId = id
                const windows = Object.assign({}, downloadWindows); windows[id] = window; downloadWindows = windows
            } else showDownloadWindow(id, true)
        }
    }
    // Per-download windows, keyed by stable ID (never by row/selection), at most one per ID.
    property var downloadWindows: ({})
    // Automatic windows open for user-started downloads (original onNewDownloadEvent). A large
    // browser batch would otherwise flood the desktop; the rest stay visible in this list.
    readonly property int automaticWindowLimit: 3
    readonly property int downloadWindowCount: Object.keys(downloadWindows).length
    function showDownloadWindow(id: string, automatic: bool): var {
        if (!live || id === "") return null
        let window = downloadWindows[id] || null
        if (!window) {
            if (automatic && Object.keys(downloadWindows).filter(key => downloadWindows[key].automatic).length >= automaticWindowLimit) return null
            window = downloadWindowComponent.createObject(root, {service: downloads, downloadId: id, automatic: automatic})
            if (!window) return null
            const windows = Object.assign({}, downloadWindows); windows[id] = window; downloadWindows = windows
        } else if (!automatic) window.automatic = false
        // The toplevel owns hydration, hidden measurement and native mapping.
        // Calling show() here would bypass its first-presentation size contract.
        window.activate("")
        return window
    }
    function openDownloadWindow() { showDownloadWindow(downloads.selectedId, false) }
    function downloadWindowFor(id: string): var { return downloadWindows[id] || null }
    function closeDownloadWindow(id: string) {
        const window = downloadWindows[id]
        if (!window) return
        const windows = Object.assign({}, downloadWindows); delete windows[id]; downloadWindows = windows
        window.destroy()
    }
    // Settings are read when the hint arrives; a closed window never reopens for old hints.
    function onDownloadWindowHint(id: string, kind: string) {
        const integration = downloads.integration || {}
        const existing = downloadWindows[id] || null
        if (kind === "started") {
            if (integration.showDownloadProgressDialog !== false) showDownloadWindow(id, true)
        } else if (existing) {
            // Like the original: without completion windows, a progress window closes on success.
            if (kind === "completed" && integration.showDownloadCompletionDialog === false) existing.close()
        } else if (integration.showDownloadCompletionDialog !== false) showDownloadWindow(id, true)
    }
    Connections {
        target: root.live ? root.downloads : null
        ignoreUnknownSignals: true
        function onDownloadWindowRequested(id: string, kind: string) { root.onDownloadWindowHint(id, kind) }
    }
    Component {
        id: downloadWindowComponent
        DownloadWindow {
            id: downloadWindow
            onDismissed: Qt.callLater(root.closeDownloadWindow, downloadWindow.downloadId)
        }
    }
    function browserNotice(): string {
        const pending = (downloads.browserRequests || []).length
        if (pending) return pending === 1 ? qsTr("1 link request waiting") : qsTr("%1 link requests waiting").arg(pending)
        const b = downloads.browserIntegration || {}
        return ({listening: qsTr("Browser integration on port %1").arg(b.port), starting: qsTr("Browser integration starting…"),
                 failed: b.error || qsTr("Browser integration failed"), disabled: qsTr("Browser integration off")})[b.state] || qsTr("Desktop integrations disabled")
    }
    function openHttpEdit() {
        const row = downloads.downloadDetails(downloads.selectedId)
        if (httpOptionsLoader.item && row.details) (httpOptionsLoader.item as HttpOptions).openOptions(downloads.selectedId, row.details.url, row.details)
    }
    function openActionsMenu() {
        const p = actionsButton.mapToItem(root, 0, actionsButton.height)
        actionsMenu.x = p.x; actionsMenu.y = p.y; actionsMenu.open()
    }
    readonly property var menuNames: ["File", "Tasks", "Tools", "Help"]
    function openMenu(name: string) {
        const index = Math.max(0, menuNames.indexOf(name))
        const menu = [fileMenu, tasksMenu, toolsMenu, helpMenu][index]
        const button = menuButtons.itemAt(index)
        // Anchor below the menubar button that opened it, never at fixed offsets.
        const p = button ? button.mapToItem(root, 0, button.height) : Qt.point(Tokens.spacingM, Tokens.rowHeight)
        menu.x = Math.round(p.x); menu.y = Math.round(p.y); menu.open()
    }
    Shortcut { sequences: [StandardKey.New]; enabled: !root.modalOpen; onActivated: root.openAddDialog() }
    Shortcut { sequences: [StandardKey.Find]; enabled: !root.modalOpen; onActivated: { searchField.forceActiveFocus(); searchField.selectAll() } }
    component Caption: QQC2.Label { color: theme.secondary; elide: Text.ElideRight; textFormat: Text.PlainText }
    component GroupCaption: Caption {
        Layout.leftMargin: Tokens.spacingL; Layout.topMargin: Tokens.spacingL; Layout.bottomMargin: Tokens.spacingS
        font.weight: Font.DemiBold; Accessible.role: Accessible.Heading
    }
    component Body: TextBlock { colors: theme }
    component ActionButton: IconButton { tooltipAllowed: !root.modalOpen }
    component Cell: QQC2.Label {
        height: Tokens.rowHeightTall; leftPadding: Tokens.cellPadding; rightPadding: Tokens.cellPadding
        verticalAlignment: Text.AlignVCenter; color: theme.text; elide: Text.ElideRight; textFormat: Text.PlainText
    }
    component NavigationItem: SidebarItem {
        colors: theme
        Layout.fillWidth: true; Layout.leftMargin: Tokens.spacingS; Layout.rightMargin: Tokens.spacingS
    }
    component MenuSurface: QQC2.Menu { popupType: QQC2.Popup.Item; enter: null; exit: null }

    ColumnLayout {
        anchors.fill: parent; spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: Tokens.spacingS; Layout.rightMargin: Tokens.spacingS; spacing: 0
            Repeater {
                id: menuButtons
                model: [qsTr("&File"), qsTr("&Tasks"), qsTr("T&ools"), qsTr("&Help")]
                delegate: QQC2.ToolButton {
                    required property string modelData; required property int index
                    text: modelData; Accessible.name: modelData.replace("&", ""); Accessible.role: Accessible.ButtonMenu
                    onClicked: root.openMenu(root.menuNames[index])
                }
            }
            Item { Layout.fillWidth: true }
        }
        Kirigami.Separator { Layout.fillWidth: true }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
            Item {
                Layout.preferredWidth: root.width < Tokens.compactWidth ? Tokens.sidebarWidthCompact : Tokens.sidebarWidth
                Layout.fillHeight: true
                QQC2.ScrollView {
                    anchors.fill: parent; contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: Tokens.spacingXS
                        GroupCaption { text: qsTr("Downloads") }
                        Repeater {
                            model: root.navigationCategories
                            delegate: NavigationItem {
                                required property var modelData
                                text: modelData.label; iconSource: modelData.icon
                                trailing: String(root.downloads.counts[modelData.key] || 0)
                                highlighted: root.downloads.category === modelData.key
                                onClicked: root.downloads.category = modelData.key
                            }
                        }
                        GroupCaption { text: qsTr("Queues"); Layout.topMargin: Tokens.spacingXL }
                        NavigationItem {
                            text: qsTr("Main"); iconSource: "view-list-details"; visible: !root.live; enabled: false
                            Accessible.description: qsTr("Queue controls require the service")
                        }
                        Repeater {
                            model: root.live ? root.downloads.queues : []
                            delegate: NavigationItem {
                                required property var modelData
                                text: modelData.name; iconSource: modelData.active ? "media-playback-start" : "view-list-details"
                                trailing: modelData.items ? String(modelData.items.length) : ""
                                Accessible.description: modelData.active ? qsTr("Running queue") : qsTr("Stopped queue")
                                enabled: root.downloads.connectionState === "ready"; onClicked: root.openQueues(modelData.id)
                            }
                        }
                    }
                }
            }
            Kirigami.Separator { Layout.fillHeight: true }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0
                RowLayout {
                    Layout.fillWidth: true; Layout.margins: Tokens.spacingM; spacing: Tokens.spacingS
                    ActionButton {
                        objectName: "addExampleButton"; text: qsTr("New Download"); icon.name: "list-add"
                        Accessible.name: root.live ? text : qsTr("Add example download")
                        enabled: !root.live || root.downloads.connectionState === "ready"; onClicked: root.openAddDialog()
                    }
                    QQC2.ToolSeparator {}
                    ActionButton { objectName: "resumeExampleButton"; text: qsTr("Resume selected download"); icon.name: "media-playback-start"; enabled: root.downloads.canResume; onClicked: root.downloads.resumeSelected() }
                    ActionButton { objectName: "pauseExampleButton"; text: qsTr("Pause selected download"); icon.name: "media-playback-pause"; enabled: root.downloads.canPause; onClicked: root.downloads.pauseSelected() }
                    ActionButton {
                        text: root.live ? qsTr("Remove from list, keep files…") : qsTr("Remove example…"); icon.name: "list-remove"
                        enabled: root.live ? root.downloads.canRemove : root.downloads.selectedId !== ""; onClicked: root.openRemoveDialog()
                    }
                    ActionButton { objectName: "copyLinkToolbarButton"; text: qsTr("Copy Link"); icon.name: "edit-copy"; visible: root.live && root.downloads.capabilities.indexOf("download.links") >= 0; enabled: root.downloads.selectionCount > 0; onClicked: root.downloads.copyDownloadLinks() }
                    ActionButton { id: actionsButton; objectName: "exampleActionsButton"; text: qsTr("More download actions"); icon.name: "overflow-menu"; onClicked: root.openActionsMenu() }
                    ActionButton { objectName: "settingsToolbarButton"; text: qsTr("Settings"); icon.name: "configure"; enabled: root.live && root.downloads.capabilities.indexOf("settings.update") >= 0; onClicked: root.openSettings() }
                    Item { Layout.fillWidth: true }
                    Kirigami.SearchField {
                        id: searchField; objectName: "previewSearch"
                        Layout.preferredWidth: Math.max(Tokens.searchWidthMin, Math.min(Tokens.searchWidthMax, root.width * 0.25))
                        // Ctrl+F is handled above so it never steals focus from an open dialog.
                        focusSequences: []
                        placeholderText: qsTr("Search downloads…"); Accessible.name: qsTr("Search downloads")
                        maximumLength: 256; text: root.downloads.search
                        // textChanged, not textEdited: the clear action calls clear().
                        onTextChanged: if (text !== root.downloads.search) root.downloads.search = text
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Flickable {
                        id: table; objectName: "downloadTableViewport"
                        anchors.fill: parent
                        // Optional columns drop out (Date, then Time Left, then Speed) before the
                        // table ever needs horizontal scrolling at ordinary window widths.
                        // Column widths, in one place; header and rows both read them.
                        readonly property int iconWidth: Tokens.iconRow + Tokens.spacingM
                        readonly property int minimumNameWidth: 200
                        readonly property int sizeWidth: 100
                        readonly property int statusWidth: 120
                        readonly property int speedWidth: 90
                        readonly property int etaWidth: 90
                        readonly property int dateWidth: 110
                        readonly property int fixedWidth: iconWidth + sizeWidth + statusWidth
                        readonly property bool showSpeed: width >= fixedWidth + minimumNameWidth + speedWidth
                        readonly property bool showEta: width >= fixedWidth + minimumNameWidth + speedWidth + etaWidth
                        readonly property bool showDate: width >= fixedWidth + minimumNameWidth + speedWidth + etaWidth + dateWidth
                        readonly property int columnsWidth: fixedWidth + (showSpeed ? speedWidth : 0) + (showEta ? etaWidth : 0) + (showDate ? dateWidth : 0)
                        readonly property real nameWidth: contentWidth - columnsWidth
                        contentWidth: Math.max(width, columnsWidth + minimumNameWidth); contentHeight: height
                        flickableDirection: Flickable.HorizontalFlick; boundsBehavior: Flickable.StopAtBounds; clip: true
                        QQC2.ScrollBar.horizontal: QQC2.ScrollBar { policy: table.contentWidth > table.width ? QQC2.ScrollBar.AsNeeded : QQC2.ScrollBar.AlwaysOff }
                        Rectangle { anchors.fill: parent; color: "transparent"; z: -1 }
                        Column {
                            width: table.contentWidth; height: table.height
                            Rectangle {
                                id: tableHeader
                                width: parent.width; height: Tokens.rowHeight; color: Qt.alpha(theme.surface, Tokens.headerOpacity)
                                Kirigami.Separator { anchors.bottom: parent.bottom; width: parent.width }
                                Row {
                                    Item { width: table.iconWidth; height: tableHeader.height }
                                    Repeater {
                                        model: [{label: qsTr("Name"), key: "name", w: table.nameWidth, shown: true}, {label: qsTr("Size"), key: "size", w: table.sizeWidth, shown: true},
                                                {label: qsTr("Status"), key: "state", w: table.statusWidth, shown: true}, {label: qsTr("Speed"), key: "speed", w: table.speedWidth, shown: table.showSpeed},
                                                {label: qsTr("Time Left"), key: "remainingTime", w: table.etaWidth, shown: table.showEta}, {label: qsTr("Date Added"), key: "dateAdded", w: table.dateWidth, shown: table.showDate}]
                                        delegate: Caption {
                                            id: headerCell
                                            required property var modelData
                                            readonly property bool sortable: root.live && root.downloads.capabilities.indexOf("settings.sort") >= 0
                                            objectName: "sortHeader-" + modelData.key
                                            visible: modelData.shown; width: modelData.w; height: tableHeader.height
                                            leftPadding: Tokens.cellPadding; rightPadding: Tokens.cellPadding
                                            font.weight: Font.DemiBold; elide: Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                            text: modelData.label + (sortable && root.downloads.sortColumn === modelData.key ? (root.downloads.sortDescending ? " ▾" : " ▴") : "")
                                            Accessible.role: Accessible.ColumnHeader
                                            Accessible.name: text; Accessible.focusable: sortable
                                            Accessible.onPressAction: if (sortable && !root.downloads.busy) root.downloads.sortBy(modelData.key)
                                            activeFocusOnTab: sortable
                                            Keys.onSpacePressed: if (sortable && !root.downloads.busy) root.downloads.sortBy(modelData.key)
                                            Keys.onReturnPressed: if (sortable && !root.downloads.busy) root.downloads.sortBy(modelData.key)
                                            MouseArea { anchors.fill: parent; enabled: headerCell.sortable && !root.downloads.busy; cursorShape: Qt.PointingHandCursor; onClicked: root.downloads.sortBy(headerCell.modelData.key) }
                                            Rectangle { anchors.fill: parent; color: "transparent"; visible: headerCell.activeFocus; border.width: Tokens.focusRingWidth; border.color: theme.focusColor }
                                        }
                                    }
                                }
                            }
                            ListView {
                                id: list; objectName: "previewDownloadList"
                                width: parent.width; height: Math.max(0, parent.height - tableHeader.height)
                                clip: true; spacing: 0; model: root.downloads; boundsBehavior: Flickable.StopAtBounds
                                activeFocusOnTab: true; keyNavigationEnabled: false; currentIndex: -1
                                Accessible.role: Accessible.List; Accessible.name: qsTr("Downloads")
                                QQC2.ScrollBar.vertical: QQC2.ScrollBar {}
                                Keys.onPressed: event => {
                                    if (event.key === Qt.Key_A && event.modifiers & Qt.ControlModifier) {
                                        root.downloads.selectAll(); event.accepted = true; return
                                    }
                                    if (event.key === Qt.Key_Escape) { root.downloads.select(""); list.currentIndex = -1; event.accepted = true; return }
                                    const current = list.currentIndex >= 0 && list.currentIndex < list.count ? list.currentIndex : root.downloads.indexOf(root.downloads.selectedId)
                                    let next = current
                                    if (event.key === Qt.Key_Down) next = Math.min(list.count - 1, current + 1)
                                    else if (event.key === Qt.Key_Up) next = Math.max(0, current - 1)
                                    else if (event.key === Qt.Key_Home) next = 0
                                    else if (event.key === Qt.Key_End) next = list.count - 1
                                    else return
                                    if (next >= 0) {
                                        const id = root.downloads.idAt(next)
                                        if (event.modifiers & Qt.ShiftModifier) root.downloads.selectRange(id, Boolean(event.modifiers & Qt.ControlModifier))
                                        else if (!(event.modifiers & Qt.ControlModifier)) root.downloads.select(id)
                                        list.currentIndex = next; list.positionViewAtIndex(next, ListView.Contain)
                                    }
                                    event.accepted = true
                                }
                                Keys.onSpacePressed: event => {
                                    if (event.modifiers & Qt.ControlModifier) root.downloads.toggleSelected(root.downloads.idAt(list.currentIndex))
                                    else if (root.downloads.canPause) root.downloads.pauseSelected()
                                    else if (root.downloads.canResume) root.downloads.resumeSelected()
                                }
                                Keys.onDeletePressed: root.openRemoveDialog()
                                Keys.onReturnPressed: { if (root.singleSelection) root.activateDownload(root.downloads.selectedId) }
                                Keys.onMenuPressed: {
                                    const item = list.currentItem
                                    if (!item) return
                                    const p = item.mapToItem(root, table.iconWidth + Tokens.cellPadding, item.height)
                                    actionsMenu.x = Math.round(p.x); actionsMenu.y = Math.round(p.y); actionsMenu.open()
                                }
                                delegate: Rectangle {
                                    id: row
                                    required property string stableId
                                    required property string fileName
                                    required property string transferState
                                    required property real progress
                                    required property string sizeLabel
                                    required property string detailLabel
                                    required property string iconName
                                    required property string dateLabel
                                    required property string speedLabel
                                    required property string etaLabel
                                    required property int index
                                    required property bool isSelected
                                    readonly property bool selected: isSelected
                                    width: ListView.view.width; height: Tokens.rowHeightTall
                                    color: selected ? Qt.alpha(theme.selected, Tokens.selectionOpacity) : hover.hovered ? Qt.alpha(theme.hover, Tokens.hoverOpacity) : index % 2 ? Qt.alpha(theme.alternate, Tokens.alternateOpacity) : "transparent"
                                    Accessible.role: Accessible.ListItem
                                    Accessible.name: fileName + ", " + root.stateLabel(transferState) + ", " + sizeLabel
                                    Accessible.description: detailLabel; Accessible.selected: selected; Accessible.focusable: true
                                    Accessible.onPressAction: { list.currentIndex = index; root.downloads.select(stableId); list.forceActiveFocus() }
                                    HoverHandler { id: hover }
                                    MouseArea {
                                        anchors.fill: parent; acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        onClicked: mouse => {
                                            if (mouse.button === Qt.RightButton) {
                                                if (!root.downloads.isSelected(row.stableId)) root.downloads.select(row.stableId)
                                                list.currentIndex = row.index; list.forceActiveFocus()
                                                const p = row.mapToItem(root, mouse.x, mouse.y)
                                                actionsMenu.x = Math.round(p.x); actionsMenu.y = Math.round(p.y); actionsMenu.open()
                                            } else root.selectRow(row.stableId, mouse.modifiers)
                                        }
                                        onDoubleClicked: mouse => {
                                            if (mouse.button === Qt.LeftButton && mouse.modifiers === Qt.NoModifier) {
                                                root.downloads.select(row.stableId); root.activateDownload(row.stableId)
                                            }
                                        }
                                    }
                                    QQC2.ToolTip.visible: hover.hovered && !root.modalOpen
                                    QQC2.ToolTip.delay: Tokens.toolTipDelay
                                    QQC2.ToolTip.text: fileName + "\n" + detailLabel
                                    Row {
                                        Item {
                                            width: table.iconWidth; height: row.height
                                            Kirigami.Icon { anchors.centerIn: parent; width: Tokens.iconRow; height: Tokens.iconRow; source: root.categoryIcon(row.iconName) }
                                        }
                                        Column {
                                            width: table.nameWidth; anchors.verticalCenter: parent.verticalCenter
                                            QQC2.Label { width: parent.width; leftPadding: Tokens.cellPadding; rightPadding: Tokens.cellPadding; text: row.fileName; textFormat: Text.PlainText; color: theme.text; elide: Text.ElideMiddle }
                                            Caption { width: parent.width; leftPadding: Tokens.cellPadding; rightPadding: Tokens.cellPadding; text: root.categoryLabel(row.stableId, row.fileName); font: Tokens.smallFont }
                                        }
                                        Cell { width: table.sizeWidth; text: row.sizeLabel }
                                        Column {
                                            width: table.statusWidth; anchors.verticalCenter: parent.verticalCenter; spacing: Tokens.spacingS
                                            QQC2.Label {
                                                leftPadding: Tokens.cellPadding; rightPadding: Tokens.cellPadding; width: parent.width; text: root.stateLabel(row.transferState); textFormat: Text.PlainText
                                                color: row.transferState === "completed" ? theme.positive : row.transferState === "failed" ? theme.error : theme.text
                                                elide: Text.ElideRight
                                            }
                                            ProgressTrack {
                                                colors: theme
                                                visible: row.transferState === "downloading" || row.transferState === "paused"
                                                x: Tokens.cellPadding; width: parent.width - 2 * Tokens.cellPadding; height: Tokens.progressHeightThin
                                                value: Math.max(0, row.progress)
                                                Accessible.role: Accessible.ProgressBar
                                                Accessible.name: qsTr("Download progress: %1 percent").arg(Math.round(row.progress * 100))
                                            }
                                        }
                                        Cell { visible: table.showSpeed; width: table.speedWidth; text: row.speedLabel; color: theme.secondary }
                                        Cell { visible: table.showEta; width: table.etaWidth; text: row.etaLabel; color: theme.secondary }
                                        Cell { visible: table.showDate; width: table.dateWidth; text: row.dateLabel; color: theme.secondary }
                                    }
                                    // Drawn above the cells so a selected/hovered row never hides keyboard focus.
                                    Rectangle {
                                        anchors.fill: parent; color: "transparent"; visible: list.activeFocus && list.currentIndex === row.index
                                        border.width: Tokens.focusRingWidth; border.color: theme.focusColor
                                    }
                                }
                            }
                        }
                    }
                    // Outside the horizontally scrolling content so it stays centered in the visible viewport.
                    ColumnLayout {
                        objectName: "downloadsEmptyState"
                        anchors.centerIn: parent; anchors.verticalCenterOffset: tableHeader.height / 2
                        width: Math.min(Tokens.messageWidth, parent.width - 2 * Tokens.spacingXXL); spacing: Tokens.spacingM
                        visible: root.downloads.visibleCount === 0
                        readonly property string state: root.live && root.downloads.connectionState === "connecting" ? "connecting"
                            : root.live && root.downloads.connectionState !== "ready" ? "disconnected"
                            : root.filtered ? "filtered" : "empty"
                        Kirigami.Icon {
                            Layout.alignment: Qt.AlignHCenter; implicitWidth: Tokens.iconLarge; implicitHeight: Tokens.iconLarge
                            source: parent.state === "disconnected" ? "network-disconnect" : parent.state === "filtered" ? "edit-find" : "folder-downloads"
                        }
                        SectionHeading {
                            level: Tokens.headingHero; horizontalAlignment: Text.AlignHCenter
                            text: ({connecting: qsTr("Connecting to the service…"), disconnected: qsTr("Service unavailable"),
                                    filtered: qsTr("No matching downloads"), empty: qsTr("No downloads yet")})[parent.state]
                        }
                        Body {
                            tone: "secondary"; horizontalAlignment: Text.AlignHCenter
                            text: ({connecting: qsTr("Connecting to the download service…"),
                                    disconnected: qsTr("Waiting for the download service…"),
                                    filtered: qsTr("Change the search text or choose another category."),
                                    empty: qsTr("Use New Download to add a link.")})[parent.state]
                        }
                    }
                }
            }
        }
        Kirigami.Separator { Layout.fillWidth: true; visible: root.live }
        RowLayout {
            objectName: "downloadsStatusBar"; visible: root.live
            Layout.fillWidth: true; Layout.leftMargin: Tokens.spacingL; Layout.rightMargin: Tokens.spacingL
            Layout.topMargin: Tokens.spacingS; Layout.bottomMargin: Tokens.spacingS; spacing: Tokens.spacingL
            Caption { objectName: "downloadsSelectedSummary"; Layout.fillWidth: true; text: root.live && root.downloads.selectionCount > 0 ? qsTr("%1 selected · %2").arg(root.downloads.selectionCount).arg(root.downloads.statistics.selectedSizeLabel) : ""; elide: Text.ElideRight }
            Caption { objectName: "downloadsQueuedSummary"; text: root.live ? qsTr("Queued: %1").arg(root.downloads.connectionState === "ready" ? root.downloads.statistics.queuedCount : "—") : "" }
            Caption { objectName: "downloadsSpeedSummary"; text: root.live ? qsTr("Total: %1").arg(root.downloads.connectionState === "ready" ? root.downloads.statistics.speedLabel : "—") : "" }
        }
        // Conditional connection/error/intake notices remain separate from the status bar.
        RowLayout {
            visible: root.downloads.errorMessage !== "" || !root.live || !root.explicitRuntime || root.downloads.connectionState !== "ready" || (root.downloads.browserRequests || []).length > 0
            Layout.fillWidth: true; Layout.leftMargin: Tokens.spacingL; Layout.rightMargin: Tokens.spacingL
            Layout.topMargin: Tokens.spacingS; Layout.bottomMargin: Tokens.spacingS; spacing: Tokens.spacingL
            QQC2.Label {
                id: statusText; objectName: "downloadsStatusNotice"
                Layout.fillWidth: true; horizontalAlignment: Text.AlignRight; textFormat: Text.PlainText
                wrapMode: root.downloads.errorMessage ? Text.Wrap : Text.NoWrap; elide: root.downloads.errorMessage ? Text.ElideNone : Text.ElideLeft
                color: root.downloads.errorMessage ? theme.error : theme.secondary
                text: root.downloads.errorMessage || (root.live ? (root.downloads.connectionState !== "ready" ? root.connectionLabel() : root.explicitRuntime ? root.browserNotice() : qsTr("Test profile · Loopback fixture downloads only · Desktop integrations disabled")) : qsTr("Interface preview · Example data only · No files are created"))
                Accessible.role: root.downloads.errorMessage ? Accessible.AlertMessage : Accessible.StaticText
                Accessible.name: text
            }
            UiButton {
                objectName: "reviewBrowserRequests"; flat: true; text: qsTr("Review…"); icon.name: "internet-web-browser"
                visible: root.live && (root.downloads.browserRequests || []).length > 0 && !root.modalOpen
                Accessible.name: qsTr("Review browser download requests")
                onClicked: root.reviewBrowserRequests()
            }
        }
    }
    MenuSurface {
        id: fileMenu; objectName: "fileMenu"
        QQC2.MenuItem { text: qsTr("&New Download…"); icon.name: "list-add"; onTriggered: root.openAddDialog() }
        QQC2.MenuItem { objectName: "addMultipleDownloads"; text: qsTr("Add &Multiple Downloads…"); icon.name: "list-add"; visible: root.live && root.downloads.capabilities.indexOf("links.review") >= 0; enabled: !root.downloads.busy; onTriggered: root.openMultipleAdd() }
        QQC2.MenuItem {
            objectName: "importClipboardLinks"; text: qsTr("New Download from &Clipboard…"); icon.name: "edit-paste"
            visible: root.live && root.downloads.capabilities.indexOf("links.import") >= 0
            enabled: root.explicitRuntime && !root.downloads.busy
            onTriggered: root.downloads.importFromClipboard()
        }
    }
    MenuSurface {
        id: tasksMenu; objectName: "tasksMenu"
        QQC2.MenuItem { objectName: "startAllQueues"; text: qsTr("&Start All Queues"); icon.name: "media-playback-start"; visible: root.live && root.downloads.capabilities.indexOf("queue.startAll") >= 0; enabled: !root.downloads.busy; onTriggered: root.downloads.taskCommand("queue.startAll") }
        QQC2.MenuItem { objectName: "stopAllQueues"; text: qsTr("Stop All &Queues"); icon.name: "media-playback-stop"; visible: root.live && root.downloads.capabilities.indexOf("queue.stopAll") >= 0; enabled: !root.downloads.busy; onTriggered: root.downloads.taskCommand("queue.stopAll") }
        QQC2.MenuItem { objectName: "stopAllDownloads"; text: qsTr("Stop All &Downloads"); icon.name: "media-playback-pause"; visible: root.live && root.downloads.capabilities.indexOf("download.stopAll") >= 0; enabled: !root.downloads.busy; onTriggered: root.downloads.taskCommand("download.stopAll") }
    }
    MenuSurface {
        id: actionsMenu; objectName: "exampleActionsMenu"
        QQC2.MenuItem { text: qsTr("&Resume"); icon.name: "media-playback-start"; enabled: root.downloads.canResume; onTriggered: root.downloads.resumeSelected() }
        QQC2.MenuItem { text: qsTr("&Pause"); icon.name: "media-playback-pause"; enabled: root.downloads.canPause; onTriggered: root.downloads.pauseSelected() }
        QQC2.MenuSeparator {}
        QQC2.MenuItem { objectName: "openCompletedDownload"; text: qsTr("&Open File"); icon.name: "document-open"; visible: root.live && root.singleSelection && root.downloads.downloadState(root.downloads.selectedId).state === "completed"; enabled: root.currentFileUrl.toString() !== ""; onTriggered: DesktopOpener.openFile(root.currentFileUrl, root.Window.window) }
        QQC2.MenuItem { objectName: "revealDownload"; text: qsTr("Show in &Folder"); icon.name: "folder-open"; visible: root.live && root.singleSelection; enabled: root.currentFolderUrl.toString() !== ""; onTriggered: DesktopOpener.showInFolder(root.currentFileUrl.toString() !== "" ? root.currentFileUrl : root.currentFolderUrl, root.Window.window) }
        QQC2.MenuItem { objectName: "showDownloadWindow"; text: qsTr("Show in Separate &Window"); icon.name: "window-new"; enabled: root.live && root.singleSelection; onTriggered: root.openDownloadWindow() }
        QQC2.MenuItem { objectName: "showDownloadDetails"; text: qsTr("P&roperties…"); icon.name: "document-properties"; enabled: root.live && root.singleSelection && root.downloads.capabilities.indexOf("download.details") >= 0; onTriggered: root.openDetails() }
        QQC2.MenuItem { objectName: "copyDownloadLink"; text: qsTr("Copy &Link"); icon.name: "edit-copy"; visible: root.live && root.downloads.capabilities.indexOf("download.links") >= 0; enabled: root.downloads.selectionCount > 0; onTriggered: root.downloads.copyDownloadLinks() }
        QQC2.MenuItem { objectName: "openDownloadPage"; text: qsTr("Open Download &Page"); icon.name: "internet-web-browser"; visible: root.live && root.singleSelection && root.downloads.downloadPageUrl(root.downloads.selectedId).toString() !== ""; onTriggered: DesktopOpener.openWebPage(root.downloads.downloadPageUrl(root.downloads.selectedId), root.Window.window) }
        QQC2.MenuItem { objectName: "moveRenameDownload"; text: qsTr("&Move / Rename…"); icon.name: "edit-rename"; visible: root.live && root.downloads.capabilities.indexOf("download.move") >= 0; enabled: root.singleSelection && root.downloads.canMutateDownload(root.downloads.selectedId, "download.move"); onTriggered: root.openMutation("download.move") }
        QQC2.MenuItem { objectName: "resetDownload"; text: qsTr("&Reset Progress…"); icon.name: "view-refresh"; visible: root.live && root.downloads.capabilities.indexOf("download.reset") >= 0; enabled: root.singleSelection && root.downloads.canMutateDownload(root.downloads.selectedId, "download.reset"); onTriggered: root.openMutation("download.reset") }
        QQC2.MenuItem { objectName: "editDownloadOptions"; text: qsTr("&Edit Connection Options…"); icon.name: "configure"; enabled: root.live && root.singleSelection && root.downloads.canResume && root.downloads.capabilities.indexOf("download.edit") >= 0; onTriggered: root.openHttpEdit() }
        MenuSurface {
            id: queueActionMenu; title: qsTr("Add to &Queue"); icon.name: "view-list-details"
            enabled: root.live && root.downloads.selectionCount > 0 && !root.downloads.busy
            Instantiator {
                model: root.live ? root.downloads.queues : []
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.name; enabled: !modelData.active
                    onTriggered: root.downloads.addToQueue(modelData.id, root.downloads.selectedIds)
                }
                onObjectAdded: (index, object) => queueActionMenu.insertItem(index, object)
                onObjectRemoved: (index, object) => queueActionMenu.removeItem(object)
            }
        }
        MenuSurface {
            id: categoryActionMenu; title: qsTr("Set &Category"); icon.name: "folder"
            enabled: root.live && root.downloads.selectionCount > 0 && !root.downloads.busy
            QQC2.MenuItem { text: qsTr("No Category"); onTriggered: root.downloads.assignCategory("none", root.downloads.selectedIds) }
            Instantiator {
                model: root.live ? root.downloads.categories : []
                delegate: QQC2.MenuItem { required property var modelData; text: modelData.name; onTriggered: root.downloads.assignCategory(modelData.id, root.downloads.selectedIds) }
                onObjectAdded: (index, object) => categoryActionMenu.insertItem(index + 1, object)
                onObjectRemoved: (index, object) => categoryActionMenu.removeItem(object)
            }
        }
        QQC2.MenuItem { text: qsTr("Select &All"); icon.name: "edit-select-all"; enabled: root.downloads.visibleCount > 0; onTriggered: root.downloads.selectAll() }
        QQC2.MenuSeparator {}
        QQC2.MenuItem { text: root.live ? qsTr("Re&move from List, Keep Files…") : qsTr("Re&move Example…"); icon.name: "list-remove"; enabled: root.live ? root.downloads.canRemove : root.downloads.selectedId !== ""; onTriggered: root.openRemoveDialog() }
    }
    MenuSurface {
        id: toolsMenu; objectName: "toolsMenu"
        QQC2.MenuItem { text: qsTr("&Queues…"); icon.name: "view-list-details"; enabled: root.live && root.downloads.capabilities.indexOf("queue.create") >= 0; onTriggered: root.openQueues("0") }
        QQC2.MenuItem { text: qsTr("&Categories…"); icon.name: "folder"; enabled: root.live && root.downloads.capabilities.indexOf("category.create") >= 0; onTriggered: root.openCategories() }
        QQC2.MenuSeparator {}
        QQC2.MenuItem { text: qsTr("Configure &Download Manager…"); icon.name: "configure"; enabled: root.live && root.downloads.capabilities.indexOf("settings.update") >= 0; onTriggered: root.openSettings() }
    }
    MenuSurface {
        id: helpMenu; objectName: "helpMenu"
        QQC2.MenuItem { text: qsTr("&About Download Manager Preview"); icon.name: "help-about"; onTriggered: aboutDialog.open() }
    }
    UiDialog {
        id: multipleAddDialog; objectName: "multipleAddDialog"
        title: qsTr("Add Multiple Downloads"); preferredWidth: Tokens.dialogWidthMedium
        preferredHeight: Tokens.dialogHeightMedium
        onOpened: multipleReview.highlighted = true
        contentItem: QQC2.ScrollView {
            clip: true; contentWidth: availableWidth
            QQC2.TextArea { id: multipleLinks; objectName: "multipleAddLinks"; placeholderText: qsTr("Download links"); textFormat: TextEdit.PlainText; wrapMode: TextEdit.Wrap; selectByMouse: true; Accessible.name: qsTr("Download links") }
        }
        footer: DialogFooter {
            UiButton {
                id: multipleReview; objectName: "reviewMultipleLinks"; text: qsTr("Review"); icon.name: "view-list-details"; highlighted: true
                enabled: root.live && !root.downloads.busy && multipleLinks.text.trim() !== "" && multipleLinks.text.length <= 256 * 1024
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
                onClicked: { if (root.downloads.reviewLinks(multipleLinks.text)) multipleAddDialog.close() }
            }
            UiButton { text: qsTr("Cancel"); icon.name: "dialog-cancel"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: multipleAddDialog.close() }
        }
    }
    UiDialog {
        id: mutationDialog; objectName: "downloadMutationDialog"
        property string operation: "download.reset"
        property string targetId: ""
        property string targetName: ""
        readonly property bool moving: operation === "download.move"
        title: moving ? qsTr("Move / Rename Download") : qsTr("Reset Download Progress?")
        preferredWidth: Tokens.dialogWidthMedium
        onOpened: mutationConfirm.highlighted = true
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            Body { text: mutationDialog.targetName; Layout.fillWidth: true }
            Body { visible: !mutationDialog.moving; text: qsTr("Downloaded progress will be discarded without starting the download."); Layout.fillWidth: true }
            FormGrid {
                visible: mutationDialog.moving; Layout.fillWidth: true
                FormLabel { text: qsTr("Name:") }
                FormField { id: moveName; objectName: "moveDownloadName"; Layout.fillWidth: true; maximumLength: 180; Accessible.name: qsTr("File name") }
                FormLabel { text: qsTr("Location:") }
                FormField { id: moveFolder; objectName: "moveDownloadFolder"; Layout.fillWidth: true; readOnly: !root.explicitRuntime; Accessible.name: qsTr("Download location") }
            }
        }
        footer: DialogFooter {
            UiButton {
                id: mutationConfirm; objectName: "confirmDownloadMutation"; text: mutationDialog.moving ? qsTr("Move / Rename") : qsTr("Reset")
                icon.name: mutationDialog.moving ? "edit-rename" : "view-refresh"
                highlighted: true; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
                enabled: root.live && root.downloads.canMutateDownload(mutationDialog.targetId, mutationDialog.operation) && (!mutationDialog.moving || (moveName.text.trim() !== "" && moveFolder.text.trim() !== ""))
                onClicked: {
                    if (mutationDialog.moving ? root.downloads.moveDownload(mutationDialog.targetId, moveName.text, moveFolder.text) : root.downloads.resetDownload(mutationDialog.targetId)) mutationDialog.close()
                }
            }
            UiButton { text: qsTr("Cancel"); QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: mutationDialog.close() }
        }
    }
    UiDialog {
        id: aboutDialog; objectName: "aboutPreviewDialog"
        title: qsTr("About Download Manager Preview"); preferredWidth: Tokens.dialogWidthSmall
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            SectionHeading { level: Tokens.headingHero; text: qsTr("AB Download Manager") }
            Body { text: root.live ? qsTr("Qt / KDE edition, using the original download engine.") : qsTr("Qt / KDE interface preview with example data.") }
        }
        footer: DialogFooter {
            UiButton { text: qsTr("Close"); icon.name: "dialog-close"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: aboutDialog.close() }
        }
    }
    UiDialog {
        id: addDialog; objectName: "addExampleDialog"; property var categoryChoices: []
        title: root.live ? qsTr("New Download") : qsTr("Add Example Download")
        preferredWidth: Tokens.dialogWidthMedium
        onClosed: { root.addOptions = ({}); sourceInput.text = ""; nameInput.text = "" }
        // Known duplicates are shown while typing, before Download is pressed. The lookups
        // are synchronous, so they run once typing pauses rather than on every key.
        property var existing: null
        property bool fileExists: false
        function checkExisting(): void {
            if (!root.live || !visible || sourceInput.text.trim() === "") { existing = null; fileExists = false; return }
            const folder = root.explicitRuntime ? locationInput.text : root.downloads.runtime.destination
            const matches = root.downloads.findDuplicates(sourceInput.text, nameInput.text, folder)
            existing = matches.length ? matches[0] : null
            fileExists = root.downloads.destinationExists(folder, nameInput.text)
        }
        Timer { id: existingCheck; interval: 250; onTriggered: addDialog.checkExisting() }
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            FormGrid {
                FormLabel { text: qsTr("Download link:") }
                FormField { id: sourceInput; objectName: "exampleUrlInput"; Layout.fillWidth: true; Accessible.name: qsTr("Download link"); maximumLength: 2048; placeholderText: root.live && !root.explicitRuntime ? "http://127.0.0.1:PORT/file" : "https://"; onTextChanged: existingCheck.restart() }
                FormLabel { text: qsTr("File name:") }
                FormField { id: nameInput; objectName: "exampleNameInput"; Layout.fillWidth: true; Accessible.name: qsTr("File name"); maximumLength: 180; onAccepted: addExample.clicked(); onTextChanged: existingCheck.restart() }
                FormLabel { text: qsTr("Save in:") }
                FormField {
                    id: locationInput; objectName: "addDownloadLocation"; Layout.fillWidth: true; readOnly: !root.explicitRuntime
                    color: readOnly ? theme.secondary : theme.text; Accessible.name: qsTr("Download location")
                    Accessible.description: readOnly ? qsTr("Fixed by the test profile") : ""
                    onTextChanged: existingCheck.restart()
                }
                FormLabel { text: qsTr("Category:") }
                UiComboBox { id: addCategory; objectName: "addCategorySelection"; Layout.fillWidth: true; enabled: root.live && root.downloads.capabilities.indexOf("category.create") >= 0; model: addDialog.categoryChoices; textRole: "name"; Accessible.name: qsTr("Download category") }
                Item { implicitWidth: 1; visible: root.live }
                UiButton {
                    objectName: "openAddHttpOptions"; text: qsTr("Connection && Authentication…"); icon.name: "configure"; visible: root.live
                    Accessible.name: qsTr("Connection and authentication options")
                    enabled: root.live && root.downloads.capabilities.indexOf("download.http-options") >= 0
                    onClicked: { if (httpOptionsLoader.item) (httpOptionsLoader.item as HttpOptions).openOptions("", "", root.addOptions) }
                }
            }
            Kirigami.InlineMessage {
                objectName: "addExistingWarning"
                Layout.fillWidth: true
                type: Kirigami.MessageType.Warning
                visible: Boolean(addDialog.existing) || addDialog.fileExists
                text: addDialog.existing ? qsTr("“%1” is already in your downloads.").arg(addDialog.existing.name) : qsTr("A file with this name already exists here.")
                actions: [
                    Kirigami.Action {
                        text: qsTr("Show Existing"); visible: Boolean(addDialog.existing)
                        onTriggered: { root.showDownloadWindow(addDialog.existing.id, false); addDialog.close() }
                    }
                ]
            }
            Body {
                tone: "secondary"; visible: !root.explicitRuntime
                text: root.live ? qsTr("Only loopback HTTP test links with a port are allowed.") : qsTr("Example data only. No files are created.")
            }
            StatusMessage { type: Kirigami.MessageType.Error; message: root.downloads.errorMessage }
        }
        footer: DialogFooter {
            UiButton { objectName: "confirmStartDownload"; text: qsTr("Download"); icon.name: "download"; highlighted: true; visible: root.live; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole; onClicked: { if (root.submitDownload(true)) addDialog.accept() } }
            UiButton { id: addExample; objectName: "confirmAddExample"; text: root.live ? qsTr("Add Paused") : qsTr("Add Example"); icon.name: "list-add"; highlighted: !root.live; QQC2.DialogButtonBox.buttonRole: root.live ? QQC2.DialogButtonBox.AlternateRole : QQC2.DialogButtonBox.AcceptRole; onClicked: { if (root.submitDownload(false)) addDialog.accept() } }
            UiButton { text: qsTr("Cancel"); icon.name: "dialog-cancel"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: addDialog.reject() }
        }
    }
    UiDialog {
        id: duplicateDialog; objectName: "duplicateDownloadDialog"
        property var matches: []
        property bool fileExists: false
        property bool start: false
        readonly property var existing: matches.length ? matches[0] : null
        readonly property bool canReplace: root.live && root.downloads.capabilities.indexOf("download.replace") >= 0
            && matches.some(m => m.sameDestination) && !matches.some(m => m.sameDestination && (m.state === "downloading" || m.state === "queued"))
        title: qsTr("Download Already Exists")
        preferredWidth: Tokens.dialogWidthMedium
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            Body { text: duplicateDialog.existing ? qsTr("“%1” is already in your downloads.").arg(duplicateDialog.existing.name) : qsTr("A file with this name already exists here.") }
            StatusMessage { type: Kirigami.MessageType.Error; message: root.downloads.errorMessage }
        }
        footer: DialogFooter {
            leading: [
                UiButton {
                    text: qsTr("Show Existing"); flat: true; visible: Boolean(duplicateDialog.existing)
                    onClicked: { root.showDownloadWindow(duplicateDialog.existing.id, false); duplicateDialog.close(); addDialog.close() }
                },
                UiButton {
                    text: qsTr("Open File"); flat: true
                    visible: Boolean(duplicateDialog.existing && duplicateDialog.existing.fileUrl.toString() !== "")
                    onClicked: { DesktopOpener.openFile(duplicateDialog.existing.fileUrl, root.Window.window); duplicateDialog.close() }
                },
                UiButton {
                    text: qsTr("Resume Existing"); flat: true
                    visible: Boolean(duplicateDialog.existing && ["paused", "failed"].indexOf(duplicateDialog.existing.state) >= 0)
                    onClicked: { if (root.downloads.resumeDownload(duplicateDialog.existing.id)) { duplicateDialog.close(); addDialog.close() } }
                }
            ]
            UiButton { objectName: "duplicateAddNew"; text: qsTr("Add New"); icon.name: "list-add"; highlighted: true; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole; onClicked: { if (root.sendDownload(duplicateDialog.start, "numbered")) { duplicateDialog.close(); addDialog.accept() } } }
            UiButton { objectName: "duplicateOverwrite"; text: qsTr("Overwrite"); icon.name: "edit-delete"; enabled: duplicateDialog.canReplace; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.DestructiveRole; onClicked: { if (root.sendDownload(duplicateDialog.start, "overwrite")) { duplicateDialog.close(); addDialog.accept() } } }
            UiButton { text: qsTr("Cancel"); icon.name: "dialog-cancel"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: duplicateDialog.reject() }
        }
    }
    UiDialog {
        id: removeDialog; objectName: "removeExampleDialog"
        property string targetId: ""; property string targetName: ""; property var targetIds: []
        title: !root.live ? qsTr("Remove Example?") : targetIds.length > 1 ? qsTr("Remove %1 Downloads?").arg(targetIds.length) : qsTr("Remove Download?")
        preferredWidth: Tokens.dialogWidthSmall
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            Body {
                objectName: "removeDownloadDescription"
                text: qsTr("“%1” will be removed from the list.").arg(removeDialog.targetName)
            }
            QQC2.CheckBox {
                id: removeDeleteFiles; objectName: "removeDeleteFiles"
                text: qsTr("Also delete files from disk")
                visible: root.live && root.downloads.capabilities.indexOf("download.delete") >= 0
            }
            StatusMessage {
                type: Kirigami.MessageType.Warning; visible: root.live && root.downloads.connectionState !== "ready"
                message: qsTr("Disconnected. Removal is unavailable.")
            }
        }
        footer: DialogFooter {
            UiButton {
                objectName: "confirmRemoveExample"; icon.name: removeDeleteFiles.checked ? "edit-delete" : "list-remove"
                text: !root.live ? qsTr("Remove Example") : removeDeleteFiles.checked ? qsTr("Delete") : qsTr("Remove")
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
                enabled: root.live ? !root.downloads.busy && root.downloads.canForgetDownloads(removeDialog.targetIds) : JSON.stringify(root.downloads.selectedIds) === JSON.stringify(removeDialog.targetIds)
                onClicked: { if (root.live) { if (root.downloads.forgetDownloads(removeDialog.targetIds, removeDeleteFiles.visible && removeDeleteFiles.checked)) removeDialog.accept() } else if (JSON.stringify(root.downloads.selectedIds) === JSON.stringify(removeDialog.targetIds)) { root.downloads.removeSelected(); removeDialog.accept() } }
            }
            UiButton { text: qsTr("Cancel"); icon.name: "dialog-cancel"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: removeDialog.reject() }
        }
    }
}
