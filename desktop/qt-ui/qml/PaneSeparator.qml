import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Vertical line between a sidebar and its page, as in the main window. Inside a padded
// UiDialog it extends through the padding, so it runs from the window top to the
// footer separator instead of floating between the panes.
Item {
    objectName: "paneSeparator"
    property int bleed: Tokens.dialogPadding
    Layout.fillHeight: true
    implicitWidth: Tokens.separatorWidth
    Kirigami.Separator {
        anchors { top: parent.top; bottom: parent.bottom; topMargin: -parent.bleed; bottomMargin: -parent.bleed }
        width: parent.width
    }
}
