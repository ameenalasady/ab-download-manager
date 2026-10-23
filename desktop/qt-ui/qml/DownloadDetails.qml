pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    required property var service
    readonly property bool modalOpen: dialog.visible
    PreviewTheme { id: theme }
    function openDetails(id: string) {
        dialog.targetId = id
        dialog.row = service.downloadDetails(id)
        if (dialog.row.id) dialog.open()
    }
    Connections {
        target: root.service
        function onViewChanged() {
            if (dialog.visible) dialog.row = root.service.downloadDetails(dialog.targetId)
        }
    }
    component PlainText: TextBlock { colors: theme }
    component SelectableText: QQC2.TextArea {
        Layout.fillWidth: true
        // Break URLs/paths at separators where possible, never mid-word first.
        readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
        textFormat: TextEdit.PlainText; color: theme.text
        Accessible.description: text
    }
    UiDialog {
        id: dialog; objectName: "downloadDetailsDialog"
        property string targetId: ""
        property var row: ({})
        readonly property var details: row.details || ({})
        readonly property var reason: details.error || null
        title: reason ? qsTr("Download Error") : qsTr("Download Properties")
        preferredWidth: Tokens.dialogWidthMedium; preferredHeight: Tokens.dialogHeightMedium
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            QQC2.ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: Tokens.spacingM
                    SectionHeading { level: Tokens.headingPage; text: dialog.row.name || qsTr("Download no longer available"); wrapMode: Text.WrapAtWordBoundaryOrAnywhere }
                    PlainText { text: dialog.reason ? dialog.reason.title : qsTr("Status: %1").arg(dialog.row.state || "—"); tone: dialog.reason ? "error" : "secondary"; font.weight: dialog.reason ? Font.DemiBold : Font.Normal }
                    PlainText { Layout.topMargin: Tokens.spacingS; text: qsTr("Download link:"); tone: "secondary" }
                    SelectableText { objectName: "downloadDetailsUrl"; text: dialog.details.url || ""; Accessible.name: qsTr("Download link") }
                    PlainText { text: qsTr("Location:"); tone: "secondary" }
                    SelectableText { text: dialog.details.folder || ""; Accessible.name: qsTr("Download location") }
                    PlainText { text: qsTr("Received: %1 bytes · Size: %2").arg(dialog.row.downloaded || "0").arg(dialog.row.size === "-1" ? qsTr("Unknown") : qsTr("%1 bytes").arg(dialog.row.size || "—")) }
                    PlainText { objectName: "downloadErrorDescription"; Layout.topMargin: Tokens.spacingM; visible: Boolean(dialog.reason); text: dialog.reason ? dialog.reason.description : "" }
                    SectionHeading { Layout.topMargin: Tokens.spacingS; visible: Boolean(dialog.reason); text: qsTr("Suggestion") }
                    PlainText { visible: Boolean(dialog.reason); text: dialog.reason ? dialog.reason.suggestion : "" }
                    StatusMessage {
                        type: Kirigami.MessageType.Warning; visible: dialog.row.state === "failed" && !dialog.reason
                        message: qsTr("The error reason is unavailable.")
                    }
                    StatusMessage {
                        type: Kirigami.MessageType.Warning; visible: root.service.connectionState !== "ready"
                        message: qsTr("Disconnected. Details may be outdated.")
                    }
                    StatusMessage { type: Kirigami.MessageType.Error; message: root.service.errorMessage }
                }
            }
        }
        footer: DialogFooter {
            UiButton {
                objectName: "retryDownloadDetails"; text: dialog.row.state === "failed" ? qsTr("Retry") : qsTr("Resume")
                icon.name: dialog.row.state === "failed" ? "view-refresh" : "media-playback-start"
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
                visible: ["failed", "paused", "queued"].indexOf(dialog.row.state) >= 0
                enabled: root.service.connectionState === "ready" && !root.service.busy && ["failed", "paused", "queued"].indexOf(dialog.row.state) >= 0
                onClicked: { if (root.service.resumeDownload(dialog.targetId)) dialog.close() }
            }
            UiButton { text: qsTr("Close"); icon.name: "dialog-close"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: dialog.close() }
        }
    }
}
