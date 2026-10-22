import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Footer for top-level windows. Same separator, padding and right alignment as
// DialogFooter, but buttons wrap on narrow windows instead of overflowing. Order is
// declaration order (KDE: affirmative first); this keeps Tab order visual.
// `leading` holds auxiliary/dismissive actions, kept on the left like DialogFooter.
ColumnLayout {
    id: footer
    default property alias buttons: actions.data
    property alias leading: leadingRow.data
    spacing: 0
    Kirigami.Separator { Layout.fillWidth: true }
    Item {
        Layout.fillWidth: true
        implicitHeight: Math.max(actions.implicitHeight, leadingRow.implicitHeight) + 2 * Tokens.spacingL
        Row {
            id: leadingRow
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.leftMargin: Tokens.dialogPadding
            anchors.topMargin: Tokens.spacingL
            spacing: Tokens.spacingM
        }
        Flow {
            id: actions
            // Width of all visible buttons on one row; the flow never grows beyond it,
            // so a single row hugs the right edge like a dialog button box.
            readonly property real naturalWidth: {
                let width = 0, count = 0
                for (let i = 0; i < children.length; ++i) {
                    if (!children[i].visible) continue
                    width += children[i].implicitWidth; ++count
                }
                return width + Math.max(0, count - 1) * spacing
            }
            readonly property real leadingWidth: leadingRow.implicitWidth > 0 ? leadingRow.implicitWidth + Tokens.spacingXL : 0
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: Tokens.dialogPadding
            anchors.topMargin: Tokens.spacingL
            width: Math.min(parent.width - 2 * Tokens.dialogPadding - leadingWidth, naturalWidth)
            spacing: Tokens.spacingM
        }
    }
}
