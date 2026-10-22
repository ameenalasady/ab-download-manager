pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Service-only forms. Drafts are copied on open/selection, never overwritten by polling.
// Shared UiDialog provides identical native transient material/chrome for every workflow.
Item {
    id: root
    required property var service
    readonly property bool modalOpen: settingsDialog.visible || queuesDialog.visible
    PreviewTheme { id: theme }
    function openSettings() { settingsDialog.loadDraft(); settingsDialog.open() }
    function openQueues(id: string) { queuesDialog.choose(id || "0"); queuesDialog.open() }
    component Body: TextBlock { colors: theme }
    component Note: Body { Layout.columnSpan: 2; tone: "secondary" }
    component Section: SettingsSection { colors: theme }
    component Navigation: SidebarItem { colors: theme }
    component RowLabel: Body { Layout.alignment: Qt.AlignVCenter }
    // Every section's controls share one column width, so fields, combo boxes and
    // check boxes line up across sections and pages.
    readonly property int controlWidth: Tokens.controlColumnWidth
    component Field: FormField { Layout.preferredWidth: root.controlWidth }
    component Check: QQC2.CheckBox { Layout.preferredWidth: root.controlWidth }
    component SectionApply: UiButton { Layout.columnSpan: 2; Layout.alignment: Qt.AlignRight; icon.name: "dialog-ok-apply" }
    readonly property bool displaySettings: service.capabilities.indexOf("settings.display") >= 0
    readonly property bool policySettings: service.capabilities.indexOf("settings.policy") >= 0
    readonly property bool browserSettings: service.capabilities.indexOf("browser.settings") >= 0
    readonly property bool integrationSettings: service.capabilities.indexOf("settings.integration") >= 0
    readonly property bool windowSettings: service.capabilities.indexOf("download.windows") >= 0
    readonly property bool explicitRuntime: service.runtime && service.runtime.mode === "explicit-runtime"
    // Original titles of retained-but-inactive settings (DesktopSettingsComponent/CommonSettings).
    readonly property var retainedTitles: ({
        autoStartOnBoot: qsTr("Start On Boot"), useSystemTray: qsTr("Use System Tray"),
        notificationSound: qsTr("Notification Sound"), generalNotificationSound: qsTr("General Notification Sound"),
        errorNotificationSound: qsTr("Error Notification Sound"), successNotificationSound: qsTr("Success Notification Sound"),
        apiEnabled: qsTr("Browser Integration"), apiPort: qsTr("Server Port"), apiAuthEnabled: qsTr("Use API Key"), apiAuthKey: qsTr("API Key"),
        trackDeletedFilesOnDisk: qsTr("Track Deleted Files On Disk"),
        deletePartialFileOnDownloadCancellation: qsTr("Delete Partial File On Download Cancellation"),
        defaultDownloadFolder: qsTr("Default Download Folder"), ignoreSSLCertificates: qsTr("Ignore SSL Certificates"),
        userAgent: qsTr("Default User Agent")
    })
    function retainedValue(entry: var): string {
        // Absent keys use the original's built-in default.
        if (!entry.stored) return qsTr("Default")
        if (entry.value === true) return qsTr("On")
        if (entry.value === false) return qsTr("Off")
        if (typeof entry.value === "number") return String(entry.value)
        return qsTr("Set (not shown)")
    }
    // Original choices: size offers bytes only, speed offers bytes and bits. A stored value
    // outside the offered list (edited file) is still shown, and kept unless changed.
    function unitChoices(offered: var, stored: string): var {
        return offered.indexOf(stored) >= 0 || !stored ? offered : offered.concat([stored])
    }
    function unitTitle(unit: string, speed: bool): string {
        const decimal = unit.startsWith("Decimal"), bits = unit.endsWith("Bits")
        const short = (decimal ? "K" : "Ki") + (bits ? "b" : "B")
        return speed ? qsTr("%1/s (%2 %3/s)").arg(short).arg(decimal ? 1000 : 1024).arg(bits ? qsTr("Bits") : qsTr("Bytes")) : short
    }
    UiDialog {
        id: settingsDialog; objectName: "serviceSettingsDialog"
        property int page: 1
        property string storedSpeedMode: "Disabled"
        readonly property var sizeChoices: root.unitChoices(["BinaryBytes", "DecimalBytes"], root.service.settings.sizeUnit || "")
        readonly property var speedChoices: root.unitChoices(["BinaryBytes", "DecimalBytes", "BinaryBits", "DecimalBits"], root.service.settings.speedUnit || "")
        function loadDraft() {
            const s = root.service.settings
            defaultFolder.text = root.service.runtime.destination || ""
            duplicateAction.currentIndex = Math.max(0, ["ask", "numbered", "existing"].indexOf(s.qtDuplicateAction || "ask"))
            threads.text = String(s.threadCount); concurrent.text = String(s.maxConcurrentDownloads)
            retries.text = String(s.maxDownloadRetryCount); minPart.text = String(s.minPartSize)
            dynamic.checked = Boolean(s.dynamicPartCreation); sparse.checked = Boolean(s.useSparseFileAllocation)
            modified.checked = Boolean(s.useServerLastModifiedTime)
            average.checked = Boolean(s.useAverageSpeed)
            storedSpeedMode = s.useSpeedLimit || "Disabled"
            speedMode.currentIndex = ["Disabled", "Enabled", "EnabledWhenBessy"].indexOf(storedSpeedMode)
            speedLimit.text = String(s.speedLimit || "262144")
            useCategory.checked = s.useCategoryByDefault !== false
            sizeUnit.currentIndex = Math.max(0, sizeChoices.indexOf(s.sizeUnit))
            speedUnit.currentIndex = Math.max(0, speedChoices.indexOf(s.speedUnit))
            const b = root.service.browserIntegration || {}
            apiEnabled.checked = b.enabled !== false; apiPort.text = String(b.port || 15151)
            apiAuth.checked = Boolean(b.auth); apiKey.text = ""
            const i = root.service.integration || {}
            startOnBoot.checked = i.autoStartOnBoot !== false
            systemTray.checked = i.useSystemTray !== false
            notificationSound.checked = i.notificationSound !== false
            progressWindow.checked = i.showDownloadProgressDialog !== false
            completionWindow.checked = i.showDownloadCompletionDialog !== false
        }
        title: qsTr("Settings")
        preferredWidth: Tokens.dialogWidthLarge; preferredHeight: Tokens.dialogHeightLarge
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: Tokens.paneSpacing
                ColumnLayout {
                    Layout.preferredWidth: Tokens.listPaneWidth; Layout.minimumWidth: Tokens.listPaneWidth; Layout.maximumWidth: Tokens.listPaneWidth
                    Layout.alignment: Qt.AlignTop; spacing: Tokens.spacingXS
                    Repeater {
                        model: [qsTr("Appearance"), qsTr("Downloads"), qsTr("Desktop Integration")]
                        delegate: Navigation {
                            required property string modelData; required property int index
                            Layout.fillWidth: true; text: modelData; highlighted: settingsDialog.page === index
                            iconSource: ["preferences-desktop-theme-global", "download", "preferences-desktop"][index]
                            onClicked: settingsDialog.page = index
                        }
                    }
                }
                QQC2.ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                    ColumnLayout {
                        width: parent.width; spacing: Tokens.contentSpacing
                        SectionHeading { level: Tokens.headingPage; text: [qsTr("Appearance"), qsTr("Downloads"), qsTr("Desktop Integration")][settingsDialog.page] }
                        Body {
                            tone: "secondary"; visible: settingsDialog.page === 2
                            text: (root.browserSettings || root.integrationSettings) && root.explicitRuntime
                                  ? qsTr("Clipboard links are only imported when you ask.")
                                  : qsTr("Desktop and browser integration are off in this preview.")
                        }
                        Section {
                            visible: settingsDialog.page === 2 && root.integrationSettings && (root.explicitRuntime || root.windowSettings)
                            RowLabel { text: qsTr("Start On Boot"); visible: root.explicitRuntime }
                            Check { id: startOnBoot; objectName: "settingsStartOnBoot"; visible: root.explicitRuntime; Accessible.name: qsTr("Allow background startup from the installed autostart entry") }
                            RowLabel { text: qsTr("Use System Tray"); visible: root.explicitRuntime }
                            Check { id: systemTray; objectName: "settingsSystemTray"; visible: root.explicitRuntime; Accessible.name: qsTr("Use system tray while the frontend is running") }
                            RowLabel { text: qsTr("Notification Sound"); visible: root.explicitRuntime }
                            Check { id: notificationSound; objectName: "settingsNotificationSound"; visible: root.explicitRuntime; Accessible.name: qsTr("Play desktop notification sounds") }
                            RowLabel { text: qsTr("Show Download Progress Window"); visible: root.windowSettings }
                            Check { id: progressWindow; objectName: "settingsProgressWindow"; visible: root.windowSettings; Accessible.name: qsTr("Open a progress window when you start a download") }
                            RowLabel { text: qsTr("Show Download Completion Window"); visible: root.windowSettings }
                            Check { id: completionWindow; objectName: "settingsCompletionWindow"; visible: root.windowSettings; Accessible.name: qsTr("Open a window when a download you started finishes or fails") }
                            Note {
                                visible: root.explicitRuntime
                                text: qsTr("Closing the window keeps downloads running in the tray.")
                            }
                            SectionApply {
                                objectName: "applyDesktopIntegration"
                                text: qsTr("Apply Desktop Integration"); enabled: !root.service.busy
                                // Only fields shown on this page are written.
                                onClicked: {
                                    const patch = root.explicitRuntime ? {autoStartOnBoot: startOnBoot.checked, useSystemTray: systemTray.checked, notificationSound: notificationSound.checked} : {}
                                    if (root.windowSettings) { patch.showDownloadProgressDialog = progressWindow.checked; patch.showDownloadCompletionDialog = completionWindow.checked }
                                    root.service.updateIntegration(patch)
                                }
                            }
                        }
                        Section {
                            id: browserFields
                            visible: settingsDialog.page === 2 && root.browserSettings
                            title: qsTr("Browser Integration")
                            readonly property var status: root.service.browserIntegration || {}
                            Body {
                                objectName: "browserIntegrationStatus"; Layout.columnSpan: 2
                                tone: browserFields.status.state === "failed" ? "error" : "secondary"
                                text: ({listening: qsTr("Listening on 127.0.0.1:%1.").arg(browserFields.status.port), starting: qsTr("Starting…"),
                                        disabled: qsTr("Off"),
                                        unavailable: qsTr("Not available for this profile."),
                                        failed: browserFields.status.error || qsTr("Could not start.")})[browserFields.status.state] || ""
                                Accessible.role: browserFields.status.state === "failed" ? Accessible.AlertMessage : Accessible.StaticText
                            }
                            RowLabel { text: qsTr("Browser Integration") }
                            Check { id: apiEnabled; objectName: "settingsApiEnabled"; Accessible.name: qsTr("Enable browser integration") }
                            RowLabel { text: qsTr("Server Port") }
                            Field { id: apiPort; objectName: "settingsApiPort"; Accessible.name: qsTr("Browser integration port"); validator: IntValidator { bottom: 1; top: 65535 } }
                            RowLabel { text: qsTr("Use API Key") }
                            Check { id: apiAuth; objectName: "settingsApiAuth"; Accessible.name: qsTr("Require an API key") }
                            RowLabel { text: qsTr("API Key"); tone: apiAuth.checked ? "normal" : "secondary" }
                            Field {
                                id: apiKey; objectName: "settingsApiKey"; enabled: apiAuth.checked
                                echoMode: TextInput.Password; maximumLength: 256
                                // Write-only: the stored key is never sent to this window.
                                placeholderText: browserFields.status.auth ? qsTr("Unchanged") : qsTr("Required")
                                Accessible.name: qsTr("API key, leave empty to keep the stored key")
                                validator: RegularExpressionValidator { regularExpression: /\S{0,256}/ }
                            }
                            Note {
                                text: qsTr("Use the same port and key in the browser extension.")
                            }
                        }
                        Section {
                            visible: settingsDialog.page === 0 && root.displaySettings
                            RowLabel { text: qsTr("Download Size Unit") }
                            UiComboBox {
                                id: sizeUnit; objectName: "settingsSizeUnit"; Layout.preferredWidth: root.controlWidth
                                model: settingsDialog.sizeChoices.map(u => root.unitTitle(u, false)); Accessible.name: qsTr("Download size unit")
                            }
                            RowLabel { text: qsTr("Download Speed Unit") }
                            UiComboBox {
                                id: speedUnit; objectName: "settingsSpeedUnit"; Layout.preferredWidth: root.controlWidth
                                model: settingsDialog.speedChoices.map(u => root.unitTitle(u, true)); Accessible.name: qsTr("Download speed unit")
                            }
                        }
                        Section {
                            visible: settingsDialog.page === 2 && root.policySettings
                            Repeater {
                                model: [{policy: "platform", title: qsTr("Inactive Settings")},
                                        {policy: "runtime-override", title: qsTr("Replaced by the Explicit Runtime Policy")}]
                                delegate: ColumnLayout {
                                    id: retainedGroup
                                    required property var modelData
                                    Layout.columnSpan: 2; Layout.fillWidth: true; Layout.topMargin: Tokens.spacingS; spacing: Tokens.formRowSpacing
                                    SectionHeading { text: retainedGroup.modelData.title }
                                    Repeater {
                                        model: (root.service.settingsPolicy.retained || []).filter(e => e.policy === retainedGroup.modelData.policy)
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true; spacing: Tokens.formColumnSpacing
                                            Accessible.role: Accessible.StaticText
                                            Accessible.name: valueTitle.text + ": " + valueText.text
                                            Body { id: valueTitle; wrapMode: Text.NoWrap; elide: Text.ElideRight; text: root.retainedTitles[modelData.key] || modelData.key }
                                            Body {
                                                id: valueText; Layout.fillWidth: false; Layout.preferredWidth: root.controlWidth; wrapMode: Text.NoWrap; elide: Text.ElideRight; tone: "secondary"
                                                text: root.retainedValue(modelData)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Section {
                            visible: settingsDialog.page === 1 && (root.service.capabilities.indexOf("settings.folder") >= 0 || root.service.capabilities.indexOf("settings.duplicates") >= 0)
                            title: qsTr("New Downloads")
                            RowLabel { text: qsTr("Default Download Folder"); visible: root.service.capabilities.indexOf("settings.folder") >= 0 }
                            Field { id: defaultFolder; objectName: "settingsDefaultFolder"; maximumLength: 4096; visible: root.service.capabilities.indexOf("settings.folder") >= 0; Accessible.name: qsTr("Default download folder"); placeholderText: qsTr("Runtime default") }
                            Note { visible: defaultFolder.visible; text: qsTr("Leave empty to use the default folder.") }
                            RowLabel { text: qsTr("Duplicate Links"); visible: root.service.capabilities.indexOf("settings.duplicates") >= 0 }
                            UiComboBox {
                                id: duplicateAction; objectName: "settingsDuplicateAction"; Layout.preferredWidth: root.controlWidth
                                visible: root.service.capabilities.indexOf("settings.duplicates") >= 0
                                model: [qsTr("Ask what to do"), qsTr("Add a numbered copy"), qsTr("Show existing download")]
                                Accessible.name: qsTr("Default duplicate-link action")
                            }
                        }
                        Section {
                            id: engineFields
                            visible: settingsDialog.page === 1
                            readonly property bool speed: root.service.capabilities.indexOf("settings.speed") >= 0
                            RowLabel { text: qsTr("Thread Count") }
                            Field { id: threads; objectName: "settingsThreadCount"; Accessible.name: qsTr("Thread count"); validator: IntValidator { bottom: 1; top: 256 } }
                            RowLabel { text: qsTr("Max Concurrent Download (0 = unlimited)") }
                            Field { id: concurrent; objectName: "settingsMaxConcurrent"; Accessible.name: qsTr("Maximum concurrent downloads, 0 for unlimited"); validator: IntValidator { bottom: 0; top: 1024 } }
                            RowLabel { text: qsTr("Maximum Download Retries") }
                            Field { id: retries; Accessible.name: qsTr("Maximum retries"); validator: IntValidator { bottom: 0; top: 1024 } }
                            RowLabel { text: qsTr("Minimum Part Size (bytes)") }
                            Field { id: minPart; Accessible.name: qsTr("Minimum part size in bytes"); validator: DoubleValidator { bottom: 1024; top: 2147483648; decimals: 0; notation: DoubleValidator.StandardNotation } }
                            RowLabel { text: qsTr("Dynamic Part Creation") }
                            Check { id: dynamic; Accessible.name: qsTr("Dynamic part creation") }
                            RowLabel { text: qsTr("Use Sparse File Allocation") }
                            Check { id: sparse; Accessible.name: qsTr("Use sparse file allocation") }
                            RowLabel { text: qsTr("Use Server Last Modified Time") }
                            Check { id: modified; Accessible.name: qsTr("Use server last modified time") }
                            RowLabel { text: qsTr("Use Average Speed"); visible: engineFields.speed }
                            Check { id: average; objectName: "settingsAverageSpeed"; Accessible.name: qsTr("Use average speed"); visible: engineFields.speed }
                            RowLabel { text: qsTr("Global Speed Limit"); visible: engineFields.speed }
                            UiComboBox {
                                id: speedMode; objectName: "settingsSpeedMode"; Layout.preferredWidth: root.controlWidth; visible: engineFields.speed
                                // A configured adapter can be selected before its first idle/input report.
                                model: root.service.settingsPolicy.activityAdapter || settingsDialog.storedSpeedMode === "EnabledWhenBessy" ? [qsTr("Disabled"), qsTr("Enabled"), qsTr("Enabled When Busy")] : [qsTr("Disabled"), qsTr("Enabled")]
                                Accessible.name: qsTr("Speed limit mode")
                            }
                            RowLabel { text: qsTr("Speed Limit (bytes/second)"); visible: engineFields.speed; tone: speedMode.currentIndex >= 1 ? "normal" : "secondary" }
                            Field {
                                id: speedLimit; objectName: "settingsSpeedLimit"; visible: engineFields.speed
                                // The value is kept (and validated) while disabled, matching the original form.
                                enabled: speedMode.currentIndex >= 1
                                Accessible.name: qsTr("Speed limit in bytes per second"); validator: RegularExpressionValidator { regularExpression: /[1-9][0-9]{0,18}/ }
                            }
                            RowLabel { text: qsTr("Use Category By Default"); visible: root.displaySettings }
                            Check { id: useCategory; objectName: "settingsUseCategory"; visible: root.displaySettings; Accessible.name: qsTr("Use category by default when adding a download") }
                        }
                        StatusMessage {
                            objectName: "settingsBusyModeNotice"; type: Kirigami.MessageType.Warning
                            visible: settingsDialog.page === 1 && speedMode.currentIndex === 2
                            message: root.service.settingsPolicy.activityAdapter
                                ? qsTr("Limits speed while you are using the computer.")
                                : qsTr("“Enabled When Busy” needs system activity monitoring, which is unavailable.")
                        }
                    }
                }
            }
            StatusMessage {
                objectName: "corruptSettingsNotice"; type: Kirigami.MessageType.Warning
                visible: (root.service.settingsPolicy.corrupt || []).length > 0
                message: qsTr("Some settings are invalid and use defaults: %1").arg((root.service.settingsPolicy.corrupt || []).join(", "))
            }
            StatusMessage { type: Kirigami.MessageType.Error; message: root.service.errorMessage }
        }
        footer: DialogFooter {
                UiButton {
                    objectName: "applyServiceSettings"; text: qsTr("Apply"); icon.name: "dialog-ok-apply"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.ApplyRole
                    // Each page applies only its own fields.
                    enabled: !root.service.busy && root.service.capabilities.indexOf("settings.update") >= 0
                        && (settingsDialog.page === 2 ? root.browserSettings && apiPort.acceptableInput
                              && (!apiAuth.checked || apiKey.text !== "" || Boolean((root.service.browserIntegration || {}).auth))
                            : settingsDialog.page === 0 ? root.displaySettings
                            : settingsDialog.page === 1 && threads.acceptableInput && concurrent.acceptableInput && retries.acceptableInput && minPart.acceptableInput
                              && (root.service.capabilities.indexOf("settings.speed") < 0 || speedLimit.acceptableInput))
                    onClicked: {
                        if (settingsDialog.page === 2) {
                            const browser = {apiEnabled: apiEnabled.checked, apiPort: Number(apiPort.text), apiAuthEnabled: apiAuth.checked}
                            if (apiKey.text !== "") browser.apiAuthKey = apiKey.text
                            if (root.service.updateBrowserSettings(browser)) apiKey.text = ""
                            return
                        }
                        if (settingsDialog.page === 0) {
                            root.service.updateSettings({sizeUnit: settingsDialog.sizeChoices[sizeUnit.currentIndex], speedUnit: settingsDialog.speedChoices[speedUnit.currentIndex]})
                            return
                        }
                        const patch = {threadCount: Number(threads.text), maxConcurrentDownloads: Number(concurrent.text),
                            maxDownloadRetryCount: Number(retries.text), minPartSize: minPart.text,
                            dynamicPartCreation: dynamic.checked, useSparseFileAllocation: sparse.checked, useServerLastModifiedTime: modified.checked}
                        if (root.service.capabilities.indexOf("settings.speed") >= 0) {
                            patch.useAverageSpeed = average.checked; patch.speedLimit = speedLimit.text
                            // Unsupported stored modes are kept; configured adapters can be selected.
                            if (speedMode.currentIndex !== 2 || root.service.settingsPolicy.activityAdapter)
                                patch.useSpeedLimit = ["Disabled", "Enabled", "EnabledWhenBessy"][speedMode.currentIndex]
                        }
                        if (root.displaySettings) patch.useCategoryByDefault = useCategory.checked
                        if (root.service.capabilities.indexOf("settings.folder") >= 0) patch.defaultDownloadFolder = defaultFolder.text
                        if (root.service.capabilities.indexOf("settings.duplicates") >= 0) patch.qtDuplicateAction = ["ask", "numbered", "existing"][duplicateAction.currentIndex]
                        root.service.updateSettings(patch)
                    }
                }
                UiButton { text: qsTr("Close"); icon.name: "dialog-close"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: settingsDialog.close() }
        }
    }
    UiDialog {
        id: queuesDialog; objectName: "serviceQueuesDialog"
        property string selectedQueue: "0"
        property int page: 0
        readonly property var dayNames: ["MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY"]
        property var scheduleDays: []
        readonly property var queue: {
            const all = root.service.queues
            for (let i = 0; i < all.length; ++i) if (all[i].id === selectedQueue) return all[i]
            return null
        }
        function choose(id: string) {
            selectedQueue = id
            const q = queue
            queueName.text = q ? q.name : ""; queueConcurrent.text = q ? String(q.maxConcurrent) : "2"
            stopOnEmpty.checked = q ? q.stopOnEmpty : false
            const s = q ? q.schedule : null
            scheduleStart.checked = s ? s.enabledStartTime : false
            scheduleStop.checked = s ? s.enabledEndTime : false
            startTime.text = s ? s.startTime.substring(0, 5) : "02:30"; endTime.text = s ? s.endTime.substring(0, 5) : "07:30"
            scheduleDays = s ? s.daysOfWeek.slice() : dayNames.slice()
        }
        function send(op: string, args: var) { root.service.queueCommand(op, selectedQueue, args || {}) }
        title: qsTr("Queues")
        preferredWidth: Tokens.dialogWidthLarge; preferredHeight: Tokens.dialogHeightLarge
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            RowLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; spacing: Tokens.paneSpacing
                // Fixed width: the list must not resize when switching tabs.
                ColumnLayout {
                    Layout.preferredWidth: Tokens.listPaneWidth; Layout.minimumWidth: Tokens.listPaneWidth; Layout.maximumWidth: Tokens.listPaneWidth
                    Layout.fillHeight: true; spacing: Tokens.spacingS
                    QQC2.ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                        ColumnLayout {
                            width: parent.width; spacing: Tokens.spacingXS
                            Repeater {
                                model: root.service.queues
                                delegate: Navigation {
                                    required property var modelData
                                    Layout.fillWidth: true; text: modelData.name
                                    iconSource: modelData.active ? "media-playback-start" : "view-list-details"
                                    trailing: modelData.items ? String(modelData.items.length) : ""
                                    Accessible.description: modelData.active ? qsTr("Running queue") : qsTr("Stopped queue")
                                    highlighted: queuesDialog.selectedQueue === modelData.id
                                    onClicked: queuesDialog.choose(modelData.id)
                                }
                            }
                        }
                    }
                    FormField { id: newQueue; objectName: "newQueueName"; Layout.fillWidth: true; placeholderText: qsTr("New queue name"); maximumLength: 120; Accessible.name: qsTr("New queue name") }
                    RowLayout {
                        Layout.fillWidth: true; spacing: Tokens.spacingS
                        UiButton {
                            objectName: "createServiceQueue"; text: qsTr("Add"); icon.name: "list-add"; Accessible.name: qsTr("Create queue"); Layout.fillWidth: true
                            enabled: !root.service.busy && newQueue.text.trim() !== ""
                            onClicked: root.service.queueCommand("queue.create", "", {name: newQueue.text})
                        }
                        UiButton {
                            text: qsTr("Delete"); icon.name: "edit-delete"; Layout.fillWidth: true
                            Accessible.name: queuesDialog.queue ? qsTr("Delete queue %1").arg(queuesDialog.queue.name) : qsTr("Delete queue")
                            enabled: !root.service.busy && queuesDialog.queue !== null && queuesDialog.selectedQueue !== "0" && !queuesDialog.queue.active
                            onClicked: { queuesDialog.send("queue.delete", {}); queuesDialog.choose("0") }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true; spacing: Tokens.contentSpacing
                    QQC2.TabBar {
                        Layout.fillWidth: true; currentIndex: queuesDialog.page; onCurrentIndexChanged: queuesDialog.page = currentIndex
                        UiTabButton { text: qsTr("Configuration") }
                        UiTabButton { text: qsTr("Items") }
                    }
                    QQC2.ScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true; visible: queuesDialog.page === 0
                        ColumnLayout {
                            width: parent.width; spacing: Tokens.contentSpacing
                            Section {
                                title: qsTr("General")
                                RowLabel { text: qsTr("Name") }
                                Field { id: queueName; objectName: "queueName"; maximumLength: 120; Accessible.name: qsTr("Queue name") }
                                RowLabel { text: qsTr("Max Concurrent Download") }
                                Field { id: queueConcurrent; Accessible.name: qsTr("Queue concurrency"); validator: IntValidator { bottom: 1; top: 1024 } }
                            }
                            Section {
                                title: qsTr("On Completion")
                                RowLabel { text: qsTr("Automatic Stop") }
                                Check { id: stopOnEmpty; Accessible.name: qsTr("Stop queue when empty") }
                            }
                            UiButton {
                                objectName: "applyQueueSettings"; text: qsTr("Apply General Settings"); icon.name: "dialog-ok-apply"; Layout.alignment: Qt.AlignRight
                                enabled: !root.service.busy && queuesDialog.queue !== null && queueName.text.trim() !== "" && queueConcurrent.acceptableInput
                                onClicked: queuesDialog.send("queue.update", {name: queueName.text, maxConcurrent: Number(queueConcurrent.text), stopOnEmpty: stopOnEmpty.checked})
                            }
                            Section {
                                title: qsTr("Scheduler")
                                    // Check box in the label column, time in the shared control column.
                                    QQC2.CheckBox { id: scheduleStart; objectName: "queueScheduleStart"; Layout.fillWidth: true; text: qsTr("Start at"); Accessible.name: qsTr("Enable scheduled start") }
                                    Field { id: startTime; objectName: "queueStartTime"; enabled: scheduleStart.checked; placeholderText: "HH:mm"; Accessible.name: qsTr("Start time, local HH:mm"); validator: RegularExpressionValidator { regularExpression: /([01][0-9]|2[0-3]):[0-5][0-9]/ } }
                                    QQC2.CheckBox { id: scheduleStop; Layout.fillWidth: true; text: qsTr("Stop at"); Accessible.name: qsTr("Enable scheduled stop") }
                                    Field { id: endTime; enabled: scheduleStop.checked; placeholderText: "HH:mm"; Accessible.name: qsTr("Stop time, local HH:mm"); validator: RegularExpressionValidator { regularExpression: /([01][0-9]|2[0-3]):[0-5][0-9]/ } }
                                Flow {
                                    Layout.columnSpan: 2; Layout.fillWidth: true; spacing: Tokens.spacingM
                                    Repeater {
                                        model: queuesDialog.dayNames
                                        delegate: QQC2.CheckBox {
                                            required property string modelData
                                            required property int index
                                            // Stored values stay MONDAY…SUNDAY; labels follow the system locale.
                                            text: Qt.locale().dayName((index + 1) % 7, Locale.ShortFormat)
                                            Accessible.name: Qt.locale().dayName((index + 1) % 7, Locale.LongFormat)
                                            checked: queuesDialog.scheduleDays.indexOf(modelData) >= 0
                                            onToggled: {
                                                const days = queuesDialog.scheduleDays.filter(d => d !== modelData)
                                                if (checked) days.push(modelData)
                                                queuesDialog.scheduleDays = days
                                            }
                                        }
                                    }
                                }
                                SectionApply {
                                    objectName: "applyQueueSchedule"; text: qsTr("Apply Schedule")
                                    enabled: !root.service.busy && queuesDialog.queue !== null && startTime.acceptableInput && endTime.acceptableInput
                                        && queuesDialog.scheduleDays.length > 0 && root.service.capabilities.indexOf("queue.schedule") >= 0
                                    onClicked: queuesDialog.send("queue.schedule", {schedule: {daysOfWeek: queuesDialog.scheduleDays,
                                        startTime: startTime.text, endTime: endTime.text, enabledStartTime: scheduleStart.checked, enabledEndTime: scheduleStop.checked}})
                                }
                            }
                        }
                        // Items use their own content container so only one scroll surface is active.
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.fillHeight: true; visible: queuesDialog.page === 1; spacing: Tokens.spacingM
                        RowLayout {
                            Layout.fillWidth: true; spacing: Tokens.spacingM
                            Body {
                                tone: "secondary"; elide: Text.ElideMiddle; maximumLineCount: 1; wrapMode: Text.NoWrap
                                text: root.service.selectedId !== "" ? qsTr("Selected: %1").arg(root.service.selectedName) : qsTr("Select a download in the main list to add it.")
                            }
                            UiButton {
                                objectName: "addSelectedToQueue"; text: qsTr("Add to Queue"); icon.name: "list-add"
                                Accessible.name: qsTr("Add selected download to queue")
                                enabled: !root.service.busy && root.service.selectedId !== "" && queuesDialog.queue !== null
                                onClicked: queuesDialog.send("queue.add", {id: root.service.selectedId})
                            }
                        }
                        SurfaceFrame {
                            id: queueFrame
                            colors: theme; Layout.fillWidth: true; Layout.fillHeight: true
                            ListView {
                                id: queueItems
                                anchors.fill: parent; anchors.margins: Tokens.separatorWidth
                                anchors.topMargin: queueFrame.inset; anchors.bottomMargin: queueFrame.inset
                                clip: true; spacing: 0
                                Accessible.role: Accessible.List; Accessible.name: qsTr("Queue items")
                                model: queuesDialog.queue ? queuesDialog.queue.items : []
                                QQC2.ScrollBar.vertical: QQC2.ScrollBar {}
                                delegate: Rectangle {
                                    id: queueItem
                                    required property string modelData
                                    required property int index
                                    width: ListView.view.width; implicitHeight: Tokens.rowHeight
                                    color: index % 2 ? theme.alternate : theme.row
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: Tokens.cellPadding; anchors.rightMargin: Tokens.spacingS; spacing: 0
                                        Body { text: root.service.downloadName(queueItem.modelData); elide: Text.ElideMiddle; maximumLineCount: 1; wrapMode: Text.NoWrap }
                                        IconButton { text: qsTr("Move up"); icon.name: "go-up"; enabled: !root.service.busy && queueItem.index > 0; onClicked: queuesDialog.send("queue.move", {id: queueItem.modelData, direction: -1}) }
                                        IconButton { text: qsTr("Move down"); icon.name: "go-down"; enabled: !root.service.busy && queueItem.index < queueItems.count - 1; onClicked: queuesDialog.send("queue.move", {id: queueItem.modelData, direction: 1}) }
                                        IconButton { text: qsTr("Remove from queue, keep download"); icon.name: "list-remove"; enabled: !root.service.busy; onClicked: queuesDialog.send("queue.remove", {id: queueItem.modelData}) }
                                    }
                                }
                                Body {
                                    anchors.centerIn: parent; width: Math.min(parent.width - 2 * Tokens.spacingXL, Tokens.messageWidth); horizontalAlignment: Text.AlignHCenter
                                    visible: queueItems.count === 0; tone: "secondary"
                                    text: qsTr("This queue is empty.")
                                }
                            }
                        }
                    }
                }
            }
            StatusMessage { type: Kirigami.MessageType.Error; message: root.service.errorMessage }
        }
        footer: DialogFooter {
            UiButton {
                objectName: "toggleServiceQueue"; text: queuesDialog.queue && queuesDialog.queue.active ? qsTr("Stop Queue") : qsTr("Start Queue")
                icon.name: queuesDialog.queue && queuesDialog.queue.active ? "media-playback-stop" : "media-playback-start"
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.ActionRole
                enabled: !root.service.busy && queuesDialog.queue !== null
                onClicked: queuesDialog.send(queuesDialog.queue.active ? "queue.stop" : "queue.start", {})
            }
            UiButton { text: qsTr("Close"); icon.name: "dialog-close"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: queuesDialog.close() }
        }
    }
}
