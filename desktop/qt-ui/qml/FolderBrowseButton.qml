import QtQuick
import QtQuick.Controls as QQC2
import Abdm.Desktop

// Opens the user's session folder chooser and writes the pick into `field`.
IconButton {
    id: button
    required property QQC2.TextField field
    property string title: qsTr("Choose Folder")
    property string startFolder: ""
    property string pending: ""
    text: qsTr("Browse…"); icon.name: "document-open-folder"
    visible: field.visible
    enabled: field.enabled && !field.readOnly && pending === ""
    onClicked: pending = DesktopOpener.chooseFolder(title, field.text || startFolder, Window.window)
    Connections {
        target: DesktopOpener
        function onFolderChosen(token: string, path: string) {
            if (token !== button.pending) return
            button.pending = ""
            if (path !== "") { button.field.text = path; button.field.forceActiveFocus() }
        }
    }
}
