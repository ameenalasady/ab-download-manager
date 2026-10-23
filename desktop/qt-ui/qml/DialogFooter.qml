import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Footer for UiDialog. Not a bare DialogButtonBox on purpose: a Dialog would
// then accept/reject on role clicks and bypass validation. Roles only drive the
// platform button order (KDE: affirmative first, Cancel/Close last).
// `leading` holds auxiliary actions that do not close the dialog; they sit on the
// left, apart from the right-aligned decision buttons.
// The primary (highlighted, initially focused) button is the first visible, non-flat
// AcceptRole button. Qt 6.11+ re-derives every box button's `highlighted` from the
// box's defaultButton on each polish, so a per-button declaration would be cleared.
// defaultButton is chosen once: Qt destroys the previous default when it changes.
ColumnLayout {
    id: footer
    default property alias buttons: box.contentData
    property alias leading: leadingRow.data
    spacing: 0
    Kirigami.Separator { Layout.fillWidth: true }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Tokens.dialogPadding; Layout.rightMargin: Tokens.dialogPadding
        spacing: Tokens.spacingM
        Row {
            id: leadingRow
            Layout.alignment: Qt.AlignVCenter
            spacing: Tokens.spacingM
            visible: children.length > 0
        }
        QQC2.DialogButtonBox {
            id: box
            Layout.fillWidth: true
            alignment: Qt.AlignRight
            spacing: Tokens.spacingM
            leftPadding: 0; rightPadding: 0
            topPadding: Tokens.spacingL; bottomPadding: Tokens.spacingL
            function choosePrimary(): void {
                if (!("defaultButton" in box) || box.defaultButton) return
                for (let i = 0; i < box.count; ++i) {
                    const button = box.itemAt(i) as QQC2.Button
                    if (button && button.visible && !button.flat
                        && button.QQC2.DialogButtonBox.buttonRole === QQC2.DialogButtonBox.AcceptRole) {
                        box.defaultButton = button
                        return
                    }
                }
            }
            // Effective visibility is only known once the dialog is shown.
            onVisibleChanged: choosePrimary()
            Component.onCompleted: choosePrimary()
        }
    }
}
