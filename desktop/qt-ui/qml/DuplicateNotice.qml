import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Reserve action width before eliding the message. InlineMessage moves its actions
// below long text; duplicate names must never determine the action row's geometry.
Rectangle {
    id: root
    property string text: ""
    property bool hasExisting: false
    property bool canOpen: false
    signal showExisting()
    signal openFile()
    Layout.fillWidth: true
    implicitHeight: row.implicitHeight + 2 * Tokens.spacingS
    color: Qt.alpha(Kirigami.Theme.neutralTextColor, 0.12)
    border.color: Kirigami.Theme.neutralTextColor
    radius: Tokens.radius
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text
    RowLayout {
        id: row
        anchors { fill: parent; margins: Tokens.spacingS }
        spacing: Tokens.spacingS
        Kirigami.Icon {
            source: "dialog-warning"
            Layout.preferredWidth: Tokens.iconSmall; Layout.preferredHeight: Tokens.iconSmall
        }
        QQC2.Label {
            objectName: "duplicateNoticeText"
            Layout.fillWidth: true; Layout.minimumWidth: 0
            text: root.text; textFormat: Text.PlainText
            elide: Text.ElideMiddle; wrapMode: Text.NoWrap
            Accessible.name: root.text
            HoverHandler { id: hover }
            QQC2.ToolTip.visible: hover.hovered
            QQC2.ToolTip.delay: Tokens.toolTipDelay
            QQC2.ToolTip.text: root.text
        }
        UiButton {
            objectName: "duplicateNoticeShowExisting"
            text: qsTr("Show Existing"); visible: root.hasExisting
            onClicked: root.showExisting()
        }
        UiButton {
            objectName: "duplicateNoticeOpenFile"
            text: qsTr("Open File"); visible: root.canOpen
            onClicked: root.openFile()
        }
    }
}
