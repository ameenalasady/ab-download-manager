import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Errors, warnings and notices in dialogs and windows. `message` is plain text; it is
// escaped because InlineMessage renders rich text. Hidden while the message is empty.
Kirigami.InlineMessage {
    property string message: ""
    Layout.fillWidth: true
    showCloseButton: false
    visible: message !== ""
    text: message.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;").replace(/\n/g, "<br/>")
    Accessible.role: type === Kirigami.MessageType.Error || type === Kirigami.MessageType.Warning
        ? Accessible.AlertMessage : Accessible.StaticText
    Accessible.name: message
}
