import QtQuick
import QtQuick.Layouts

// Two-column label/field form (Add, connection options, browser review). Labels are
// FormLabels; fields take Layout.fillWidth.
GridLayout {
    Layout.fillWidth: true
    columns: 2
    columnSpacing: Tokens.formColumnSpacing
    rowSpacing: Tokens.formRowSpacing
}
