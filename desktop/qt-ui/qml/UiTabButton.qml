pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2

QQC2.TabButton {
    id: button
    Accessible.name: text
    ButtonFocus { control: button }
}
