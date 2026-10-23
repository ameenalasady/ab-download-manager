import QtQuick
import QtQuick.Controls as QQC2

// Icon-only tool button: `text` is the accessible name and delayed tooltip.
QQC2.ToolButton {
    id: button
    property bool tooltipAllowed: true
    display: QQC2.AbstractButton.IconOnly
    Accessible.name: text
    ButtonFocus { control: button }
    QQC2.ToolTip.visible: hovered && tooltipAllowed && text !== ""
    QQC2.ToolTip.delay: Tokens.toolTipDelay
    QQC2.ToolTip.text: text
}
