import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Duplicate warning card: a short title, an elided detail line (full text in the
// tooltip and accessible name) and compact inline tool-button actions. The actions reserve their
// width before the text elides, so a long file name never pushes them onto another
// row. Child items (e.g. the keep-both/replace choice) go below, aligned with the text.
Rectangle {
    id: root
    property string title: ""
    property string detail: ""
    property bool hasExisting: false
    property bool canOpen: false
    property bool canResume: false
    default property alias extra: extraColumn.data
    property bool extraVisible: false
    signal showExisting()
    signal openFile()
    signal resume()
    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + 2 * Tokens.spacingM
    color: Qt.alpha(Kirigami.Theme.neutralTextColor, 0.1)
    border.width: Tokens.separatorWidth
    border.color: Qt.alpha(Kirigami.Theme.neutralTextColor, 0.5)
    radius: Tokens.radius
    Accessible.role: Accessible.AlertMessage
    Accessible.name: detail ? title + ". " + detail : title
    ColumnLayout {
        id: content
        anchors {
            left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
            leftMargin: Tokens.spacingM; rightMargin: Tokens.spacingS
        }
        spacing: Tokens.spacingS
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.spacingM
            Kirigami.Icon {
                source: "dialog-warning"; Accessible.ignored: true
                Layout.preferredWidth: Tokens.iconRow; Layout.preferredHeight: Tokens.iconRow
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.minimumWidth: 0
                spacing: 0
                QQC2.Label {
                    objectName: "duplicateNoticeTitle"
                    Layout.fillWidth: true
                    text: root.title; textFormat: Text.PlainText
                    font.weight: Font.DemiBold; elide: Text.ElideRight
                }
                QQC2.Label {
                    objectName: "duplicateNoticeText"
                    Layout.fillWidth: true; Layout.minimumWidth: 0
                    visible: text !== ""
                    text: root.detail; textFormat: Text.PlainText
                    font: Tokens.smallFont; opacity: 0.8
                    elide: Text.ElideMiddle; wrapMode: Text.NoWrap
                    Accessible.name: root.detail
                    HoverHandler { id: hover }
                    QQC2.ToolTip.visible: hover.hovered && truncated
                    QQC2.ToolTip.delay: Tokens.toolTipDelay
                    QQC2.ToolTip.text: root.detail
                }
            }
            QQC2.ToolButton {
                objectName: "duplicateNoticeResume"
                display: QQC2.AbstractButton.TextBesideIcon; text: qsTr("Resume"); icon.name: "media-playback-start"
                visible: root.canResume
                Accessible.name: qsTr("Resume existing download")
                onClicked: root.resume()
            }
            QQC2.ToolButton {
                objectName: "duplicateNoticeOpenFile"
                display: QQC2.AbstractButton.TextBesideIcon; text: qsTr("Open"); icon.name: "document-open"
                visible: root.canOpen
                Accessible.name: qsTr("Open existing file")
                onClicked: root.openFile()
            }
            QQC2.ToolButton {
                objectName: "duplicateNoticeShowExisting"
                display: QQC2.AbstractButton.TextBesideIcon; text: qsTr("Show"); icon.name: "view-list-details"
                visible: root.hasExisting
                Accessible.name: qsTr("Show existing download")
                onClicked: root.showExisting()
            }
        }
        ColumnLayout {
            id: extraColumn
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.iconRow + Tokens.spacingM
            Layout.bottomMargin: Tokens.spacingXS
            visible: root.extraVisible
            spacing: Tokens.spacingS
        }
    }
}
