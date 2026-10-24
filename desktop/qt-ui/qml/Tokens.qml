// Declared in qmldir. qmllint (Qt 6.12) does not read singleton entries of an implicitly
// imported directory and reports a position-less false positive for this file only.
pragma Singleton
import QtQuick
import org.kde.kirigami as Kirigami

// The one place for spacing, sizing, radii, type levels and structural widths.
// Everything is derived from Kirigami units, so it follows the desktop's scale and
// font. Colors are not here: they come from the live palette via PreviewTheme.
QtObject {
    // Spacing scale. Kirigami spacing units are font-independent (4 and 8 px by default).
    readonly property int spacingXS: Math.max(1, Math.round(Kirigami.Units.smallSpacing / 2))
    readonly property int spacingS: Kirigami.Units.smallSpacing
    readonly property int spacingM: Kirigami.Units.largeSpacing
    readonly property int spacingL: Kirigami.Units.smallSpacing * 3
    readonly property int spacingXL: Kirigami.Units.largeSpacing * 2
    readonly property int spacingXXL: Kirigami.Units.largeSpacing * 3

    // Semantic spacing: dialogs, forms, framed sections and list/editor panes.
    readonly property int dialogPadding: spacingXL
    readonly property int dialogScreenMargin: spacingXXL + spacingXL
    readonly property int contentSpacing: spacingL
    readonly property int formRowSpacing: spacingM
    readonly property int formColumnSpacing: spacingL
    readonly property int sectionPadding: spacingL
    readonly property int paneSpacing: spacingXL
    readonly property int cellPadding: spacingM

    // Shape.
    readonly property int radius: Kirigami.Units.cornerRadius
    readonly property int focusRingWidth: 2
    readonly property int separatorWidth: 1

    // Control and row heights, following the font through gridUnit.
    readonly property int rowHeightCompact: Math.round(Kirigami.Units.gridUnit * 1.5)
    readonly property int rowHeight: Kirigami.Units.gridUnit * 2
    readonly property int rowHeightTall: Math.round(Kirigami.Units.gridUnit * 2.5)
    readonly property int progressHeightThin: Kirigami.Units.smallSpacing
    readonly property int progressHeight: Kirigami.Units.largeSpacing

    // Icon sizes: navigation and menus use small, list rows smallMedium.
    readonly property int iconSmall: Kirigami.Units.iconSizes.small
    readonly property int iconRow: Kirigami.Units.iconSizes.smallMedium
    readonly property int iconMedium: Kirigami.Units.iconSizes.medium
    readonly property int iconLarge: Kirigami.Units.iconSizes.large

    // Type scale as Kirigami.Heading levels; body and small text use the desktop fonts.
    readonly property int headingHero: 1     // About title, empty states
    readonly property int headingTitle: 2    // dialog titles
    readonly property int headingPage: 3     // page/editor titles inside a dialog
    readonly property int headingSection: 5  // framed section and group titles
    readonly property font smallFont: Kirigami.Theme.smallFont

    // Structural widths in logical pixels (they follow display scaling, not the font):
    // the download table's optional columns are budgeted against the sidebar widths.
    readonly property int sidebarWidth: 200
    readonly property int sidebarWidthCompact: 174
    readonly property int compactWidth: 850
    readonly property int searchWidthMin: 160
    readonly property int searchWidthMax: 260
    readonly property int listPaneWidth: 190
    readonly property int controlColumnWidth: 220
    readonly property int messageWidth: 380   // centered empty-state and placeholder text

    // Top-level windows: shared minimum, default size by window role.
    // Window resizes and page fades between review, progress and completion; follows the
    // Plasma animation speed (0 when animations are off).
    readonly property int pageTransitionMs: Kirigami.Units.longDuration
    readonly property int windowMinimumWidth: 360
    readonly property int windowMinimumHeight: 260
    readonly property int windowMaximumHeight: 16777215 // Qt's unconstrained window maximum
    readonly property int initialConfigureGuardMs: 1800
    readonly property int windowHydrationTimeoutMs: 1000
    readonly property int downloadWindowWidth: 560
    readonly property int completionWindowWidth: 440
    readonly property int downloadWindowHeight: 760 // progress + eight part rows before scrolling
    readonly property int reviewWindowWidth: 720
    readonly property int reviewTableMinimumWidth: 680
    readonly property int reviewNameWidth: 220
    readonly property int reviewSizeWidth: 90
    readonly property int reviewWindowHeight: 580
    readonly property int visiblePartRows: 8
    readonly property int partBytesWidth: 85
    readonly property int partTotalWidth: 80

    // Dialog widths/heights by role; UiDialog clamps them to the window.
    readonly property int dialogWidthSmall: 500
    readonly property int dialogWidthMedium: 640
    readonly property int dialogWidthLarge: 860
    readonly property int dialogHeightMedium: 540
    readonly property int dialogHeightLarge: 620

    // Material is applied only to painted surfaces, never to text or the opaque palette.
    readonly property real materialOpacity: 0.86
    readonly property real headerOpacity: 0.14
    readonly property real alternateOpacity: 0.10
    readonly property real hoverOpacity: 0.45
    readonly property real selectionOpacity: 0.78

    // Same dim as Kirigami's own dialogs: palette-independent by design.
    readonly property color scrim: Qt.rgba(0, 0, 0, 0.3)
    readonly property int toolTipDelay: Kirigami.Units.toolTipDelay
}
