pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Abdm.Desktop

// A distinct completion page, not a progress pane with a few controls disabled.
// Mirrors the original completion dialog: file identity/size, success, Open actions.
// Opening the file or its folder is the end of this window's job, so both close it.
Item {
    id: completion
    required property var row
    required property url fileUrl
    signal closeRequested()
    readonly property bool openable: fileUrl.toString() !== ""
    implicitHeight: page.implicitHeight
    PreviewTheme { id: theme }
    ColumnLayout {
        id: page
        anchors { left: parent.left; right: parent.right; top: parent.top }
        spacing: 0
        ColumnLayout {
            id: summary
            Layout.fillWidth: true; Layout.margins: Tokens.dialogPadding
            spacing: Tokens.contentSpacing
            RowLayout {
                Layout.fillWidth: true; spacing: Tokens.spacingXL
                ColumnLayout {
                    Layout.alignment: Qt.AlignTop; spacing: Tokens.spacingS
                    Kirigami.Icon { Layout.alignment: Qt.AlignHCenter; implicitWidth: Tokens.iconLarge; implicitHeight: implicitWidth; source: completion.row.iconName || "text-x-generic" }
                    QQC2.Label { Layout.alignment: Qt.AlignHCenter; text: completion.row.sizeLabel || ""; textFormat: Text.PlainText; color: theme.secondary; font: Tokens.smallFont }
                }
                ColumnLayout {
                    Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; spacing: Tokens.spacingS
                    RowLayout {
                        Layout.fillWidth: true; spacing: Tokens.spacingS
                        Kirigami.Icon { implicitWidth: Tokens.iconRow; implicitHeight: implicitWidth; source: "emblem-ok-symbolic"; color: theme.positive }
                        SectionHeading { objectName: "downloadCompletionHeading"; Layout.fillWidth: true; level: Tokens.headingTitle; text: qsTr("Download Complete") }
                    }
                    QQC2.Label {
                        objectName: "downloadCompletionName"; Layout.fillWidth: true
                        text: completion.row.name || ""; textFormat: Text.PlainText; wrapMode: Text.WrapAnywhere
                        maximumLineCount: 3; elide: Text.ElideRight
                        font.weight: Font.DemiBold; color: theme.text
                    }
                    QQC2.Label {
                        Layout.fillWidth: true; text: completion.row.folder || ""; visible: text !== ""
                        textFormat: Text.PlainText; elide: Text.ElideMiddle; color: theme.secondary
                    }
                }
            }
            TextBlock { colors: theme; tone: "secondary"; visible: !completion.openable; text: qsTr("File not found. It may have been moved or deleted.") }
        }
        WindowFooter {
            Layout.fillWidth: true
            UiButton {
                objectName: "downloadWindowOpenFile"; text: qsTr("Open File"); icon.name: "document-open"; highlighted: true
                enabled: completion.openable
                onClicked: { DesktopOpener.openFile(completion.fileUrl, completion.Window.window); completion.closeRequested() }
            }
            UiButton {
                objectName: "downloadWindowOpenFolder"; text: qsTr("Open Folder"); icon.name: "folder-open"
                enabled: completion.openable
                onClicked: { DesktopOpener.showInFolder(completion.fileUrl, completion.Window.window); completion.closeRequested() }
            }
            UiButton { objectName: "downloadCompletionClose"; text: qsTr("Close"); icon.name: "dialog-close"; onClicked: completion.closeRequested() }
        }
    }
}
