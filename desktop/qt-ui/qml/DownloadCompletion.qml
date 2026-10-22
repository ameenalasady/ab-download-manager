pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Abdm.Desktop

// A distinct completion page, not a progress pane with a few controls disabled.
// Mirrors the original completion dialog: file identity/size, success, Open actions.
Item {
    id: completion
    required property var row
    required property url fileUrl
    signal closeRequested()
    implicitHeight: summary.implicitHeight + 2 * Tokens.dialogPadding
    PreviewTheme { id: theme }
    QQC2.ScrollView {
        id: scroll
        anchors.fill: parent; contentWidth: availableWidth; clip: true
        ColumnLayout {
            id: summary
            x: Tokens.dialogPadding; width: Math.max(0, scroll.availableWidth - 2 * Tokens.dialogPadding)
            y: Tokens.dialogPadding; spacing: Tokens.contentSpacing
            RowLayout {
                Layout.fillWidth: true; spacing: Tokens.spacingXL
                ColumnLayout {
                    Layout.alignment: Qt.AlignVCenter; spacing: Tokens.spacingS
                    Kirigami.Icon { Layout.alignment: Qt.AlignHCenter; implicitWidth: Tokens.iconLarge; implicitHeight: implicitWidth; source: completion.row.iconName || "text-x-generic" }
                    QQC2.Label { Layout.alignment: Qt.AlignHCenter; text: completion.row.sizeLabel || ""; textFormat: Text.PlainText; color: theme.secondary }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: Tokens.spacingM
                    RowLayout {
                        Layout.fillWidth: true; spacing: Tokens.spacingS
                        Kirigami.Icon { implicitWidth: Tokens.iconRow; implicitHeight: implicitWidth; source: "emblem-ok-symbolic"; color: theme.positive }
                        SectionHeading { objectName: "downloadCompletionHeading"; Layout.fillWidth: true; level: Tokens.headingTitle; text: qsTr("Download Complete") }
                    }
                    QQC2.Label {
                        objectName: "downloadCompletionName"; Layout.fillWidth: true
                        text: completion.row.name || ""; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere
                        font.weight: Font.DemiBold; color: theme.text
                    }
                    QQC2.Label {
                        Layout.fillWidth: true; text: completion.row.folder || ""; visible: text !== ""
                        textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.secondary
                    }
                }
            }
            TextBlock { colors: theme; tone: "secondary"; visible: completion.fileUrl.toString() === ""; text: qsTr("The file is unavailable for opening. It may have moved, or this test profile does not allow desktop opening.") }
            Flow {
                Layout.fillWidth: true; spacing: Tokens.spacingM
                UiButton {
                    objectName: "downloadWindowOpenFile"; text: qsTr("Open File"); icon.name: "document-open"; highlighted: true
                    enabled: completion.fileUrl.toString() !== ""
                    onClicked: DesktopOpener.openFile(completion.fileUrl, completion.Window.window)
                }
                UiButton {
                    objectName: "downloadWindowOpenFolder"; text: qsTr("Open Folder"); icon.name: "folder-open"
                    enabled: completion.fileUrl.toString() !== ""
                    onClicked: DesktopOpener.showInFolder(completion.fileUrl, completion.Window.window)
                }
                UiButton { objectName: "downloadCompletionClose"; text: qsTr("Close"); icon.name: "dialog-close"; onClicked: completion.closeRequested() }
            }
        }
    }
}
