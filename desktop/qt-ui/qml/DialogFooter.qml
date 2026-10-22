import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Footer for UiDialog. Not a bare DialogButtonBox on purpose: a Dialog would
// then accept/reject on role clicks and bypass validation. Roles only drive the
// platform button order (KDE: affirmative first, Cancel/Close last).
ColumnLayout {
    id: footer
    default property alias buttons: box.contentData
    spacing: 0
    Kirigami.Separator { Layout.fillWidth: true }
    QQC2.DialogButtonBox {
        id: box
        Layout.fillWidth: true
        alignment: Qt.AlignRight
        spacing: Tokens.spacingM
        leftPadding: Tokens.dialogPadding; rightPadding: Tokens.dialogPadding
        topPadding: Tokens.spacingL; bottomPadding: Tokens.spacingL
    }
}
