pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.Button {
    id: button
    Accessible.name: text
    readonly property bool primary: highlighted && !flat
    // Keep the deferred desktop-style delegates owned by the control. Swapping
    // them out and restoring their pointers races native popup/default handling.
    // This item paints emphasis over the native frame without replacing it.
    Rectangle {
        id: primaryFrame
        objectName: "buttonPrimaryFrame"
        anchors.fill: parent
        anchors.leftMargin: 1; anchors.rightMargin: 1
        anchors.topMargin: 1; anchors.bottomMargin: 4
        z: 1; visible: button.primary
        radius: Tokens.radius
        opacity: button.enabled ? 1 : 0.5
        color: Qt.tint(Kirigami.Theme.highlightColor, button.down ? Qt.rgba(0, 0, 0, 0.25)
            : button.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
        border.width: Tokens.separatorWidth
        border.color: Qt.lighter(Kirigami.Theme.highlightColor, 1.25)
        QQC2.Label {
            anchors.fill: parent
            text: button.Kirigami.MnemonicData.richTextLabel
            textFormat: Text.StyledText
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            color: Kirigami.Theme.highlightedTextColor
            elide: Text.ElideRight
        }
    }
    ButtonFocus { control: button }
}
