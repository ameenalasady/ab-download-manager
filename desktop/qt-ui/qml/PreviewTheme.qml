import QtQuick
import org.kde.kirigami as Kirigami

// KDE's desktop style resolves Kirigami color sets, which may differ from the
// bare QGuiApplication palette (especially in offscreen tests). Use the same
// provider as the controls, without writing any appearance preferences.
Item {
    id: theme
    objectName: "desktopTheme"
    // Keep this zero-size item visible: Kirigami defers palette resolution for
    // hidden items. Hiding it yields uninitialized black colors in offscreen runs.
    Kirigami.Theme.colorSet: Kirigami.Theme.Window
    Kirigami.Theme.inherit: false
    Item {
        id: viewColors
        Kirigami.Theme.colorSet: Kirigami.Theme.View
        Kirigami.Theme.inherit: false
    }
    readonly property font defaultFont: Kirigami.Theme.defaultFont
    readonly property color text: Kirigami.Theme.textColor
    readonly property color secondary: readableColor(Qt.tint(surface, Qt.rgba(text.r, text.g, text.b, 0.72)), 4.5)
    readonly property color surface: Kirigami.Theme.backgroundColor
    readonly property color row: viewColors.Kirigami.Theme.backgroundColor
    readonly property color alternate: viewColors.Kirigami.Theme.alternateBackgroundColor
    readonly property color hover: Qt.tint(row, Qt.rgba(accent.r, accent.g, accent.b, 0.12))
    readonly property color selected: Qt.tint(row, Qt.rgba(accent.r, accent.g, accent.b, 0.28))
    readonly property color border: Qt.tint(surface, Qt.rgba(text.r, text.g, text.b, 0.20))
    readonly property color focusColor: readableColor(Kirigami.Theme.focusColor, 3.0)
    readonly property color accent: Kirigami.Theme.highlightColor
    readonly property color positive: readableColor(Kirigami.Theme.positiveTextColor, 4.5)
    readonly property color warning: readableColor(Kirigami.Theme.neutralTextColor, 4.5)
    readonly property color error: readableColor(Kirigami.Theme.negativeTextColor, 4.5)
    readonly property color field: row

    function linear(value: real): real {
        return value <= 0.04045 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4)
    }
    function luminance(value: color): real {
        return 0.2126 * linear(value.r) + 0.7152 * linear(value.g) + 0.0722 * linear(value.b)
    }
    function contrast(a: color, b: color): real {
        const first = luminance(a)
        const second = luminance(b)
        return (Math.max(first, second) + 0.05) / (Math.min(first, second) + 0.05)
    }
    // Preserve system semantic hues, but avoid unreadable small text or focus
    // rings on our tinted row states. Never write back to the desktop palette.
    function readableColor(source: color, minimum: real): color {
        const backgrounds = [theme.surface, theme.row, theme.alternate, theme.hover, theme.selected]
        for (let step = 0; step <= 20; ++step) {
            const candidate = Qt.tint(source, Qt.rgba(theme.text.r, theme.text.g, theme.text.b, step / 20))
            if (backgrounds.every(background => contrast(candidate, background) >= minimum + 0.01))
                return candidate
        }
        return theme.text
    }
}
