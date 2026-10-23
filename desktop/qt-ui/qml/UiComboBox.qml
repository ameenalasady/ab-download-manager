pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2

// Keep the dropdown in its owning window's overlay. A second Popup.Window nested
// in a native UiDialog loses its opening grab/focus on the popup-window handoff
// (including the opening pointer release), immediately dismissing the list; in a
// top-level window it does not stay open either (offscreen and Wayland tests).
// The list opens below the field when it fits; otherwise on the side with more room,
// scrolling, instead of being shifted over the field and the rest of the form.
QQC2.ComboBox {
    id: combo
    popup.popupType: QQC2.Popup.Item
    popup.x: 0
    popup.y: combo.height
    popup.width: combo.width
    function placePopup(): void {
        const wanted = popup.implicitHeight
        const window = combo.Window.window
        if (!window) return
        const top = combo.mapToItem(null, 0, 0).y
        const below = window.height - top - combo.height - Tokens.spacingS
        const above = top - Tokens.spacingS
        if (wanted <= below || below >= above) {
            popup.y = combo.height
            popup.height = Math.min(wanted, Math.max(below, 0))
        } else {
            popup.height = Math.min(wanted, above)
            popup.y = -popup.height
        }
        // The desktop style's Menu list only scrolls when taller than the whole window.
        const list = popup.contentItem as ListView
        if (list) list.interactive = popup.height < wanted
    }
    Connections {
        target: combo.popup
        function onAboutToShow() { combo.placePopup() }
    }
}
