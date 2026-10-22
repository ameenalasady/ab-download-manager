pragma ComponentBehavior: Bound
import QtQuick
import org.kde.kirigami as Kirigami

// The KDE native button StyleItem treats activeFocus OR highlighted as hasFocus,
// painting Kvantum's rectangle around the label even for a pointer/default button.
// Suppress only that paint hint, not the control's actual focus or accessibility.
// Keyboard navigation gets an unambiguous outline around the whole hit target.
Item {
    id: frame
    required property var control
    readonly property var nativeBackground: control.background
    anchors.fill: parent
    z: 2
    Accessible.ignored: true
    function suppressNativeFocus(): void {
        // Replace the native paint binding once per background instance. No live
        // Binding/restoration object crosses the StyleItem's teardown lifecycle.
        if (nativeBackground && nativeBackground.hasFocus !== undefined)
            nativeBackground.hasFocus = false
    }
    Component.onCompleted: suppressNativeFocus()
    onNativeBackgroundChanged: suppressNativeFocus()
    Rectangle {
        objectName: "buttonKeyboardFocusFrame"
        anchors.fill: parent
        visible: frame.control.visualFocus
        color: "transparent"
        radius: Tokens.radius
        border.width: Tokens.focusRingWidth
        border.color: Kirigami.Theme.focusColor
    }
}
