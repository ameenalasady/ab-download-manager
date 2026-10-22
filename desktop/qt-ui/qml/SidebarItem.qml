pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// One list/navigation row style for the sidebar, settings pages, queues and
// categories: same height, radius, hover/selected tint and focus ring.
QQC2.ItemDelegate {
    id: item
    required property PreviewTheme colors
    property string iconSource: ""
    property string trailing: ""
    implicitHeight: Tokens.rowHeight
    leftPadding: Tokens.spacingM
    rightPadding: Tokens.spacingM
    contentItem: RowLayout {
        spacing: Tokens.spacingM
        Kirigami.Icon {
            visible: item.iconSource !== ""
            source: item.iconSource
            implicitWidth: Tokens.iconRow; implicitHeight: Tokens.iconRow
            opacity: item.enabled ? 1 : 0.5
        }
        QQC2.Label {
            Layout.fillWidth: true
            text: item.text
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: item.enabled ? item.colors.text : item.colors.secondary
        }
        QQC2.Label {
            visible: item.trailing !== ""
            text: item.trailing
            textFormat: Text.PlainText
            color: item.colors.secondary
        }
    }
    background: Rectangle {
        radius: Tokens.radius
        color: item.highlighted ? item.colors.selected : item.hovered && item.enabled ? item.colors.hover : "transparent"
        border.width: item.visualFocus ? Tokens.focusRingWidth : 0
        border.color: item.colors.focusColor
    }
    Accessible.name: trailing !== "" ? text + ", " + trailing : text
    Accessible.selected: highlighted
}
