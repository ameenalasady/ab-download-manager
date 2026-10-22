import QtQuick
import QtQuick.Layouts

// Framed group of settings: optional title, then label/control rows in two columns.
// Controls take Layout.preferredWidth: Tokens.controlColumnWidth so every section
// shares one control column; notes and buttons span with Layout.columnSpan: 2.
SurfaceFrame {
    id: section
    property string title: ""
    default property alias content: grid.data
    Layout.fillWidth: true
    implicitHeight: grid.implicitHeight + 2 * Tokens.sectionPadding
    GridLayout {
        id: grid
        anchors.fill: parent
        anchors.margins: Tokens.sectionPadding
        columns: 2
        rowSpacing: Tokens.formRowSpacing
        columnSpacing: Tokens.formColumnSpacing
        SectionHeading { Layout.columnSpan: 2; visible: section.title !== ""; text: section.title }
    }
}
