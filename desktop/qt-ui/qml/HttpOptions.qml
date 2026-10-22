pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    required property var service
    readonly property bool modalOpen: dialog.visible
    property string targetId: ""
    signal optionsAccepted(var options)
    PreviewTheme { id: theme }
    component Body: TextBlock { colors: theme }
    function openOptions(id: string, source: string, options: var) {
        targetId = id
        sourceInput.text = source
        connections.text = options.connections ? String(options.connections) : ""
        speed.text = options.speedLimit || "0"
        const draft = !id && options.credentials ? options.credentials : null
        replaceCredentials.checked = Boolean(draft)
        username.text = draft ? draft.username || "" : ""; password.text = draft ? draft.password || "" : ""
        userAgent.text = draft ? draft.userAgent || "" : ""; headers.text = draft ? JSON.stringify(draft.headers || {}) : "{}"
        localError.message = ""
        pages.currentIndex = 0
        dialog.open()
    }
    function submit(): bool {
        const count = connections.text.trim()
        if (count && (!/^[0-9]+$/.test(count) || Number(count) < 1 || Number(count) > 256)) {
            localError.message = qsTr("Connections must be blank (default) or 1–256."); return false
        }
        if (!/^[0-9]+$/.test(speed.text)) { localError.message = qsTr("Speed must be nonnegative integer bytes/sec."); return false }
        const options = {connections: count ? Number(count) : null, speedLimit: speed.text}
        if (targetId) options.url = sourceInput.text
        if (replaceCredentials.checked) {
            let parsed
            try { parsed = JSON.parse(headers.text) } catch (_) { localError.message = qsTr("Headers must be a JSON object of string values."); return false }
            if (!parsed || Array.isArray(parsed) || typeof parsed !== "object" || Object.keys(parsed).some(k => typeof parsed[k] !== "string")) {
                localError.message = qsTr("Headers must be a JSON object of string values."); return false
            }
            options.credentials = {username: username.text, password: password.text, userAgent: userAgent.text, headers: parsed}
        }
        if (targetId) {
            if (!service.editDownload(targetId, options)) return false
        } else optionsAccepted(options)
        // Do not retain typed secrets after closing; Add's explicit draft lives until submission/cancel.
        password.text = ""; username.text = ""; headers.text = "{}"; userAgent.text = ""
        dialog.accept()
        return true
    }
    UiDialog {
        id: dialog; objectName: "httpOptionsDialog"
        title: root.targetId ? qsTr("Edit Connection Options") : qsTr("Connection & Authentication")
        preferredWidth: Tokens.dialogWidthMedium; preferredHeight: Tokens.dialogHeightMedium
        onClosed: { password.text = ""; username.text = ""; headers.text = "{}"; userAgent.text = "" }
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            QQC2.TabBar {
                id: pages; objectName: "httpOptionsTabs"; Layout.fillWidth: true
                UiTabButton { text: qsTr("Connection") }
                UiTabButton { text: qsTr("Authentication") }
                UiTabButton { text: qsTr("Headers") }
            }
            QQC2.ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: Tokens.contentSpacing
                    Body {
                        tone: "secondary"; visible: pages.currentIndex === 0 && root.targetId !== ""
                        text: qsTr("Pause the download and stop its queues before editing. Applying changes keeps it paused; files are not moved or reset.")
                    }
                    FormGrid {
                        visible: pages.currentIndex === 0
                        FormLabel { visible: root.targetId !== ""; text: qsTr("Download link:") }
                        FormField { id: sourceInput; objectName: "editSourceInput"; visible: root.targetId !== ""; Layout.fillWidth: true; maximumLength: 2048; Accessible.name: qsTr("Download link") }
                        FormLabel { text: qsTr("Connections:") }
                        FormField { id: connections; objectName: "httpConnections"; Layout.fillWidth: true; maximumLength: 3; placeholderText: qsTr("Default (1–256)"); Accessible.name: qsTr("Connections") }
                        FormLabel { text: qsTr("Speed limit (bytes/s):") }
                        FormField { id: speed; objectName: "httpSpeedLimit"; Layout.fillWidth: true; maximumLength: 19; Accessible.name: qsTr("Per-download speed limit") }
                    }
                    Body { visible: pages.currentIndex === 0; tone: "secondary"; text: qsTr("A speed limit of 0 means unlimited.") }
                    QQC2.CheckBox {
                        id: replaceCredentials; objectName: "replaceHttpCredentials"; visible: pages.currentIndex !== 0; Layout.fillWidth: true
                        text: root.targetId ? qsTr("Replace stored authentication and headers") : qsTr("Use custom authentication and headers")
                    }
                    Body {
                        visible: pages.currentIndex !== 0; tone: "secondary"
                        text: root.targetId ? qsTr("Stored values are never displayed. Leave replacement off to keep them. Enabling it with blank fields clears all stored authentication and headers.") : qsTr("Enable custom values above to send authentication or headers. Only enter credentials for a trusted server.")
                    }
                    FormGrid {
                        visible: pages.currentIndex === 1
                        FormLabel { text: qsTr("Username:") }
                        FormField { id: username; objectName: "httpUsername"; enabled: replaceCredentials.checked; Layout.fillWidth: true; maximumLength: 4096; Accessible.name: qsTr("HTTP username") }
                        FormLabel { text: qsTr("Password:") }
                        FormField { id: password; objectName: "httpPassword"; enabled: replaceCredentials.checked; Layout.fillWidth: true; echoMode: TextInput.Password; maximumLength: 4096; Accessible.name: qsTr("HTTP password") }
                    }
                    StatusMessage { visible: pages.currentIndex === 1; type: Kirigami.MessageType.Warning; message: qsTr("Credentials are stored unencrypted in the original profile format. They are stripped on cross-origin redirects.") }
                    FormGrid {
                        visible: pages.currentIndex === 2
                        FormLabel { text: qsTr("User-Agent:") }
                        FormField { id: userAgent; enabled: replaceCredentials.checked; Layout.fillWidth: true; placeholderText: qsTr("Default"); maximumLength: 1024; Accessible.name: qsTr("User-Agent") }
                    }
                    // A multi-line editor reads better full width, labelled above.
                    QQC2.Label { visible: pages.currentIndex === 2; text: qsTr("Custom headers (JSON object):"); textFormat: Text.PlainText }
                    QQC2.TextArea { id: headers; objectName: "httpHeaders"; visible: pages.currentIndex === 2; enabled: replaceCredentials.checked; Layout.fillWidth: true; Layout.preferredHeight: Tokens.rowHeight * 3; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; selectByMouse: true; Accessible.name: qsTr("HTTP headers JSON") }
                    Body { visible: pages.currentIndex === 2; tone: "secondary"; text: qsTr("Example: {\"Referer\":\"https://example.test/\"}. Headers are stripped on cross-origin redirects. Stored authentication and headers are replaced together.") }
                }
            }
            StatusMessage { id: localError; type: Kirigami.MessageType.Error }
            StatusMessage { type: Kirigami.MessageType.Error; message: root.service.errorMessage }
        }
        footer: DialogFooter {
            UiButton { objectName: "applyHttpOptions"; text: root.targetId ? qsTr("Apply, Keep Paused") : qsTr("Use Options"); icon.name: "dialog-ok"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole; enabled: !root.service.busy; onClicked: root.submit() }
            UiButton { text: qsTr("Cancel"); icon.name: "dialog-cancel"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: dialog.reject() }
        }
    }
}
