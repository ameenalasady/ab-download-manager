import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Footer for UiDialog. Not a bare DialogButtonBox on purpose: a Dialog would
// then accept/reject on role clicks and bypass validation. Roles only drive the
// platform button order (KDE: affirmative first, Cancel/Close last).
// `leading` holds auxiliary actions that do not close the dialog; they sit on the
// left, apart from the right-aligned decision buttons.
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
        }
    }
}
