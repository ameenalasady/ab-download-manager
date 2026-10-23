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
    function openChecksum(id: string) {
        if (service.capabilities.indexOf("download.checksum") < 0) return
        const row = service.downloadState(id)
        if (row.state !== "completed") return
        dialog.targetId = id
        dialog.row = row
        const saved = row.fileChecksum || ""
        const parts = saved.split(":")
        algorithm.currentIndex = Math.max(0, algorithm.model.indexOf(parts[0]))
        expected.text = parts.length === 2 ? parts[1] : ""
        dialog.open()
    }
    Connections {
        target: root.service
        function onViewChanged() { if (dialog.visible) dialog.row = root.service.downloadState(dialog.targetId) }
    }
    component Body: TextBlock { colors: theme }
    UiDialog {
        id: dialog; objectName: "downloadChecksumDialog"
        property string targetId: ""
        property var row: ({})
        readonly property var task: root.service.checksum.id === targetId ? root.service.checksum : ({})
        readonly property bool running: task.state === "running"
        readonly property string selectedAlgorithm: algorithm.currentText
        readonly property string digest: task.state === "done" && task.algorithm === selectedAlgorithm ? task.digest : ""
        readonly property string entered: expected.text.trim()
        readonly property var lengths: ({"MD5": 32, "SHA-1": 40, "SHA-256": 64, "SHA-512": 128})
        readonly property bool valid: entered === "" || (entered.length === lengths[selectedAlgorithm] && /^[0-9a-fA-F]+$/.test(entered))
        readonly property string saved: row.fileChecksum || ""
        readonly property bool writable: row.state === "completed" && !root.service.busy && root.service.capabilities.indexOf("download.checksum") >= 0
        title: qsTr("File Checksum")
        preferredWidth: Tokens.dialogWidthMedium; preferredHeight: Tokens.dialogHeightMedium
        contentItem: ColumnLayout {
            spacing: Tokens.contentSpacing
            QQC2.ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth; clip: true
                ColumnLayout {
                    width: parent.width; spacing: Tokens.spacingM
                    Body {
                        text: dialog.row.name || qsTr("Download no longer available")
                        wrapMode: Text.NoWrap; elide: Text.ElideMiddle
                        Accessible.name: text
                        HoverHandler { id: nameHover }
                        QQC2.ToolTip.visible: nameHover.hovered; QQC2.ToolTip.text: text; QQC2.ToolTip.delay: Tokens.toolTipDelay
                    }
                    Body { text: qsTr("Algorithm:") }
                    UiComboBox {
                        id: algorithm; objectName: "checksumAlgorithm"; Layout.fillWidth: true
                        model: ["SHA-256", "MD5", "SHA-1", "SHA-512"]; enabled: !dialog.running
                        Accessible.name: qsTr("Checksum algorithm")
                    }
                    Body { text: qsTr("Expected checksum (optional):") }
                    FormField {
                        id: expected; objectName: "checksumExpected"; Layout.fillWidth: true; maximumLength: 256
                        Accessible.name: qsTr("Expected checksum")
                        placeholderText: qsTr("Hexadecimal value; leave empty to clear the saved checksum")
                    }
                    Body {
                        tone: "secondary"
                        text: dialog.saved !== "" ? qsTr("Saved: %1").arg(dialog.saved) : qsTr("No expected checksum saved.")
                    }
                    Body { visible: !dialog.valid; tone: "error"; text: qsTr("Enter exactly %1 hexadecimal characters.").arg(dialog.lengths[dialog.selectedAlgorithm]) }
                    Body { text: qsTr("Calculated checksum:") }
                    QQC2.TextArea {
                        objectName: "checksumDigest"; Layout.fillWidth: true
                        text: dialog.digest; readOnly: true; selectByMouse: true; wrapMode: TextEdit.WrapAnywhere
                        textFormat: TextEdit.PlainText; color: theme.text; Accessible.name: qsTr("Calculated checksum")
                    }
                    QQC2.ProgressBar { Layout.fillWidth: true; visible: dialog.running; value: (dialog.task.percent || 0) / 100 }
                    Body {
                        objectName: "checksumComparison"
                        text: dialog.running ? qsTr("Calculating… %1%").arg(dialog.task.percent || 0)
                            : dialog.task.state === "error" ? qsTr("Unable to read the completed file, or it changed during calculation.")
                            : dialog.task.state === "cancelled" ? qsTr("Calculation cancelled")
                            : dialog.digest === "" ? qsTr("Not calculated with this algorithm")
                            : dialog.entered === "" ? qsTr("Calculated; no expected checksum to compare")
                            : !dialog.valid ? qsTr("Expected checksum is invalid")
                            : dialog.digest.toLowerCase() === dialog.entered.toLowerCase() ? qsTr("Matches expected checksum") : qsTr("Does not match expected checksum")
                    }
                    Body {
                        visible: root.service.checksum.state === "running" && root.service.checksum.id !== dialog.targetId
                        text: qsTr("Another download is being checked. Reopen its File Checksum window to cancel it.")
                    }
                    Body { tone: "secondary"; text: qsTr("Calculate reads the completed file without changing it. Save Expected only changes the download record. A checksum alone does not prove authenticity. Calculation continues if this window is closed.") }
                    StatusMessage { type: Kirigami.MessageType.Error; message: root.service.errorMessage }
                }
            }
        }
        footer: DialogFooter {
            UiButton {
                objectName: "checksumStart"; text: qsTr("Calculate"); icon.name: "view-refresh"
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.AcceptRole
                enabled: dialog.writable && !dialog.running && root.service.checksum.state !== "running"
                onClicked: root.service.checksumCommand(dialog.targetId, "start", dialog.selectedAlgorithm, "")
            }
            UiButton {
                objectName: "checksumCancel"; text: qsTr("Cancel Calculation"); visible: dialog.running
                enabled: !root.service.busy
                onClicked: root.service.checksumCommand(dialog.targetId, "cancel", dialog.selectedAlgorithm, "")
            }
            UiButton {
                objectName: "checksumSave"; text: dialog.entered === "" ? qsTr("Clear Saved") : qsTr("Save Expected")
                QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.ApplyRole
                enabled: dialog.writable && dialog.valid && (dialog.entered === "" ? dialog.saved !== "" : dialog.saved.toLowerCase() !== (dialog.selectedAlgorithm + ":" + dialog.entered).toLowerCase())
                onClicked: root.service.checksumCommand(dialog.targetId, "save", dialog.selectedAlgorithm, dialog.entered)
            }
            UiButton { text: qsTr("Close"); icon.name: "dialog-close"; QQC2.DialogButtonBox.buttonRole: QQC2.DialogButtonBox.RejectRole; onClicked: dialog.close() }
        }
    }
}
