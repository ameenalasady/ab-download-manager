import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Every heading uses Kirigami's type scale at one weight. The default level is a
// section title; set `level` to a Tokens.heading* role for titles and hero text.
Kirigami.Heading {
    level: Tokens.headingSection
    type: Kirigami.Heading.Type.Primary
    Layout.fillWidth: true
    textFormat: Text.PlainText
    wrapMode: Text.Wrap
    Accessible.role: Accessible.Heading
}
