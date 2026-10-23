import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.Dialog {
    id: dialog
    property Item hostItem: null
    parent: hostItem
    enter: null
    exit: null
    opacity: 1
    QQC2.Overlay.modal: Item {
        Rectangle {
            anchors.fill: parent
            color: "#80000000"
        }
    }
    title: "Optional Kirigami dialog (synthetic)"
    preferredWidth: Kirigami.Units.gridUnit * 22
    preferredHeight: 240
    standardButtons: Kirigami.Dialog.Ok | Kirigami.Dialog.Cancel

    background: Rectangle {
        color: "#FF20202A"
        border.width: 1
        border.color: "#FF4A4A5A"
        radius: 8
    }

    Item {
        implicitWidth: Kirigami.Units.gridUnit * 20
        implicitHeight: body.implicitHeight
        QQC2.Label {
            id: body
            width: parent.width
            padding: Kirigami.Units.largeSpacing
            text: "Kirigami loaded for the optional example. This is still a synthetic capability dialog with no engine or IPC."
            color: "#FFFFFFFF"
            wrapMode: Text.Wrap
        }
    }
}
