pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Abdm.Desktop

// Shared chrome, native style background/controls, opaque foreground and a real
// separator instead of a second card. Content owns its scroll area; buttons go in
// a DialogFooter so every dialog has the same separator, padding and button order.
// Size by role with Tokens.dialogWidth*/dialogHeight*; both are clamped to the window.
QQC2.Dialog {
    id: dialog
    property int preferredWidth: Tokens.dialogWidthMedium
    property int preferredHeight: 0 // 0: fit content
    parent: QQC2.Overlay.overlay
    width: parent ? Math.min(preferredWidth, parent.width - 2 * Tokens.dialogScreenMargin) : preferredWidth
    height: preferredHeight > 0 && parent ? Math.min(preferredHeight, parent.height - 2 * Tokens.dialogScreenMargin) : implicitHeight
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: Tokens.dialogPadding
    modal: true
    focus: true
    // One architecture for Settings and every workflow: decorated transient window,
    // no inconsistent per-parent dim overlay. Modality still prevents parent actions.
    popupType: QQC2.Popup.Window
    dim: false
    enter: null
    exit: null
    // Hidden popup content does not receive a render-loop polish. Resolve nested
    // layouts and wrapped text at the real width before exposing the native popup,
    // rather than letting its first frame use pre-layout implicit heights.
    function polishTree(item: Item) {
        if (!item) return
        for (const child of item.children) polishTree(child)
        // Never force a native StyleItem/control's pending deferred construction.
        // Only layout/positioner measurement needs synchronous polish here.
        if (item instanceof ColumnLayout || item instanceof RowLayout || item instanceof GridLayout
            || item instanceof Flow || item instanceof ListView || item instanceof QQC2.ScrollView)
            item.ensurePolished()
    }
    onAboutToShow: {
        // Fixed-role dialogs already have their final geometry. In particular do
        // not force deferred native controls on unopened Settings pages to polish.
        if (preferredHeight > 0) return
        for (let pass = 0; pass < 4; ++pass) {
            polishTree(header)
            polishTree(contentItem)
            polishTree(footer)
        }
    }
    // Popup.Window reparents the content after aboutToShow. Its attached Window
    // binding can still name the owner during that handoff; resolve it once the
    // popup is actually open, then apply to the popup's own mapped surface.
    onOpened: material.window = contentItem ? contentItem.Window.window : null
    NativeWindowStyle {
        id: material
        window: dialog.contentItem ? dialog.contentItem.Window.window : null
        maximumOpacity: Tokens.materialOpacity
    }
    background: Rectangle { color: material.background }
    header: ColumnLayout {
        spacing: 0
        SectionHeading {
            Layout.leftMargin: Tokens.dialogPadding
            Layout.rightMargin: Tokens.dialogPadding
            Layout.topMargin: Tokens.spacingL
            Layout.bottomMargin: Tokens.spacingL
            level: Tokens.headingTitle
            text: dialog.title
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
        }
        Kirigami.Separator { Layout.fillWidth: true }
    }
}
