import QtQuick

// Rounded progress bar for download rows and windows. `value` is 0–1; a negative value
// means the total is unknown, which shows a sliding segment while `indeterminate`.
Rectangle {
    id: track
    required property PreviewTheme colors
    property real value: 0
    property color barColor: colors.accent
    property bool indeterminate: false
    readonly property bool known: value >= 0
    implicitHeight: Tokens.progressHeight
    radius: height / 2
    color: colors.border
    clip: true
    Rectangle {
        height: parent.height
        radius: track.radius
        color: track.barColor
        width: track.known ? parent.width * Math.min(1, track.value) : track.indeterminate ? parent.width / 4 : 0
        SequentialAnimation on x {
            running: !track.known && track.indeterminate
            loops: Animation.Infinite
            NumberAnimation { from: 0; to: track.width * 3 / 4; duration: 1200 }
            NumberAnimation { from: track.width * 3 / 4; to: 0; duration: 1200 }
        }
    }
}
