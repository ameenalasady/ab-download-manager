pragma ComponentBehavior: Bound

import QtQuick

Rectangle {
    id: root
    objectName: "backdropRoot"
    property string pattern: "stripes"

    readonly property bool striped: pattern === "stripes"
    readonly property color patternColor: pattern === "black" ? "#FF000000"
                                          : pattern === "white" ? "#FFFFFFFF"
                                          : pattern === "magenta" ? "#FFFF00FF"
                                          : "#FF0E0E14"
    readonly property color labelColor: pattern === "black" || pattern === "stripes"
                                        ? "#FFFFFFFF" : "#FF101018"

    color: patternColor

    Repeater {
        model: 14
        visible: root.striped

        Rectangle {
            required property int index
            visible: root.striped
            x: index * root.width / 14
            y: 0
            width: root.width / 14 + 1
            height: root.height
            color: Qt.hsla(index / 14, 0.78, 0.55, 1.0)
        }
    }

    Rectangle {
        visible: root.striped
        anchors.centerIn: parent
        width: Math.min(560, root.width - 80)
        height: 160
        color: "#FFFFFFFF"
        radius: 10
        border.width: 2
        border.color: "#FF101018"

        Column {
            anchors.centerIn: parent
            spacing: 10

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "PUBLIC BACKDROP"
                color: "#FF101018"
                font.pixelSize: 30
                font.bold: true
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "opaque stripe/color target • window opacity 100% • no blur"
                color: "#FF30303A"
                font.pixelSize: 14
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 20
        text: "BACKDROP " + root.pattern + " • opaque • 100% window opacity • no blur"
        color: root.labelColor
        font.pixelSize: 13
        font.bold: true
    }
}
