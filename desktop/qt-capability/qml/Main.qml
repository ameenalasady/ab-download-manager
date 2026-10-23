pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

Item {
    id: root
    objectName: "capabilityRoot"
    implicitWidth: 980; implicitHeight: 840
    property bool migrationView: false
    readonly property bool migrationReady: !migrationView || migrationLoader.status === Loader.Ready
    property string requestedProbe: "none"
    readonly property bool probeVisible: requestedProbe === "menu" ? windowMenu.visible
        : requestedProbe === "dialog" ? manualDialog.visible
        : requestedProbe === "kirigami" ? (kirigamiLoader.item !== null && kirigamiLoader.item.visible) : false
    function openProbe(name) {
        requestedProbe = name
        if (name === "menu") windowMenu.open()
        else if (name === "dialog") manualDialog.open()
        else if (name === "kirigami") kirigamiLoader.active = true
    }
    Rectangle {
        id: shell
        objectName: "shellBackground"
        anchors.fill: parent
        color: root.migrationView ? "transparent" : controller.reducedTransparency ? "#ff161620" : "#cc1b1b24"
        Loader {
            id: migrationLoader; objectName: "migrationLoader"
            anchors.fill: parent; active: root.migrationView
            property bool ready: false
            Component.onCompleted: {
                ready = true
                if (active) setSource("qrc:/qt-ui/qml/DownloadsView.qml", {downloads: previewDownloads})
            }
            onActiveChanged: if (ready && active) setSource("qrc:/qt-ui/qml/DownloadsView.qml", {downloads: previewDownloads})
            onStatusChanged: if (status === Loader.Error) console.error("Migration preview failed to load")
        }
        ColumnLayout {
            visible: !root.migrationView
            anchors.fill: parent; anchors.margins: 18; spacing: 12
            // Probe controls, not window chrome. KWin alone draws caption buttons.
            RowLayout {
                QQC2.ToolButton { objectName: "menuButton"; text: "Menu"; Accessible.name: text; onClicked: windowMenu.open() }
                QQC2.ToolButton {
                    objectName: "blurButton"; text: controller.blurRequested ? "Blur on" : "Blur off"; Accessible.name: text
                    enabled: controller.blurBridgeAvailable && !controller.reducedTransparency
                    onClicked: controller.setBlurEnabled(!controller.blurRequested)
                }
                QQC2.ToolButton {
                    objectName: "opaqueButton"; text: controller.reducedTransparency ? "Opaque on" : "Opaque off"; Accessible.name: text
                    onClicked: controller.setReducedTransparency(!controller.reducedTransparency)
                }
            }
            QQC2.Label { text: "Synthetic capability checks — no engine or production profile"; color: "white"; Layout.fillWidth: true; wrapMode: Text.Wrap }
            QQC2.Label { objectName: "diagnosticsText"; text: controller.diagnosticsText; color: "white"; Layout.fillWidth: true; wrapMode: Text.Wrap }
            Rectangle {
                objectName: "syntheticRow1"; Layout.fillWidth: true; implicitHeight: 40; color: "#2a2a36"
                QQC2.Label { anchors.centerIn: parent; text: "Synthetic row (no download)"; color: "white" }
            }
            QQC2.TextField { objectName: "manualInput"; Accessible.name: "Synthetic text input"; Layout.fillWidth: true }
            Rectangle { objectName: "opaqueSwatch"; implicitWidth: 28; implicitHeight: 28; color: "magenta" }
            Item { objectName: "translucentMeasurementRegion"; Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
    QQC2.Menu {
        id: windowMenu; x: 18; y: 50
        QQC2.MenuItem { text: "Open manual dialog"; onTriggered: manualDialog.open() }
        QQC2.MenuItem { text: "Open optional Kirigami dialog"; enabled: controller.kirigamiAvailable; onTriggered: root.openProbe("kirigami") }
    }
    QQC2.Dialog {
        id: manualDialog; title: "Manual dialog test (synthetic)"; modal: true
        width: Math.min(520, root.width - 40); height: 260
        x: (root.width - width) / 2; y: (root.height - height) / 2
        standardButtons: QQC2.Dialog.Ok | QQC2.Dialog.Cancel
        contentItem: QQC2.TextField { objectName: "dialogInput"; Accessible.name: "Synthetic dialog text input" }
    }
    Loader {
        id: kirigamiLoader; anchors.fill: parent; active: false; source: "KirigamiDialogProbe.qml"
        onLoaded: { item.hostItem = root; item.open() }
    }
}
