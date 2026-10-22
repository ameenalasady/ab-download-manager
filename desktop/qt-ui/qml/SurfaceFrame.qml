import QtQuick

// View-colored rounded frame for embedded lists and settings sections. Content should
// stay `inset` away from the top/bottom edges so square rows never cover the corners.
Rectangle {
    required property PreviewTheme colors
    readonly property int inset: Tokens.radius
    color: Qt.alpha(colors.row, Tokens.headerOpacity)
    radius: Tokens.radius
    border.width: Tokens.separatorWidth
    border.color: colors.border
}
