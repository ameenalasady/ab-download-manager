import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

// Wrapped body text in one of the theme's readable tones: `secondary` for hints and
// descriptions, semantic tones for short status text. Never interprets markup.
QQC2.Label {
    id: block
    required property PreviewTheme colors
    property string tone: "normal" // normal | secondary | positive | warning | error
    Layout.fillWidth: true
    wrapMode: Text.Wrap
    textFormat: Text.PlainText
    color: ({secondary: colors.secondary, positive: colors.positive, warning: colors.warning, error: colors.error})[tone] || colors.text
    Accessible.role: tone === "error" ? Accessible.AlertMessage : Accessible.StaticText
    Accessible.name: text
}
