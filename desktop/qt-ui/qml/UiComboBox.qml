pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2

// Keep the dropdown in its owning window's overlay. A second Popup.Window nested
// in a native UiDialog loses its opening grab/focus on the popup-window handoff
// (including the opening pointer release), immediately dismissing the list.
// The overlay also handles screen/viewport bounds for short windows.
QQC2.ComboBox {
    id: combo
    popup.popupType: QQC2.Popup.Item
    popup.x: 0
    popup.y: combo.height
    popup.width: combo.width
}
