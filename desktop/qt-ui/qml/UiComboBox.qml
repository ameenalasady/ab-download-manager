pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2

// The desktop style's list is only as wide as its longest entry and, as an in-window
// item, gets shifted up over the form when a short window cannot fit it below. Open it
// as its own popup surface, directly under the field and at the field's width.
QQC2.ComboBox {
    id: combo
    popup.popupType: QQC2.Popup.Window
    popup.x: 0
    popup.y: combo.height
    popup.width: combo.width
}
