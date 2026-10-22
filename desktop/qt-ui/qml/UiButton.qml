pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

QQC2.Button {
    id: button
    Accessible.name: text
    // The desktop style expresses `highlighted` only through the hasFocus hint that
    // ButtonFocus suppresses, and it paints the label inside that native frame. A primary
    // (highlighted) button therefore gets its own accent frame and label, at the native
    // size, so the default action stands out from its neighbours.
    readonly property bool primary: highlighted && !flat
    // Controls created inside popups/button boxes may receive their native background
    // after completion, so (re)apply whenever it changes. `highlighted` can also be
    // cleared later (e.g. by dialog default-button handling): then the native frame and
    // label are restored, never left as a stale accent.
    property Item nativeBackground: null
    property Item nativeContent: null
    function applyPrimary(): void {
        const custom = background && background.objectName === "buttonPrimaryFrame"
        if (primary && background && !custom) {
            nativeBackground = background; nativeContent = contentItem
            // The native frame is drawn inset within its item; match its visible bounds.
            topInset = 1; bottomInset = 4; leftInset = 1; rightInset = 1
            background = primaryFrame.createObject(button)
            contentItem = primaryLabel.createObject(button)
        } else if (!primary && custom && nativeBackground) {
            topInset = 0; bottomInset = 0; leftInset = 0; rightInset = 0
            background = nativeBackground; contentItem = nativeContent
            // Qt hides replaced items explicitly; restoring their pointer alone leaves
            // the native button invisible after DialogButtonBox clears its default.
            nativeBackground.visible = true
            if (nativeContent) nativeContent.visible = true
        }
    }
    // Size like the native button with the same label, so neighbours line up.
    QQC2.Button {
        id: nativeMetrics
        visible: false
        text: button.text; icon.name: button.icon.name; display: button.display
        Accessible.ignored: true
    }
    Component.onCompleted: applyPrimary()
    onBackgroundChanged: applyPrimary()
    onPrimaryChanged: applyPrimary()
    Component {
        id: primaryFrame
        Rectangle {
            objectName: "buttonPrimaryFrame"
            implicitWidth: nativeMetrics.implicitWidth - button.leftInset - button.rightInset
            implicitHeight: nativeMetrics.implicitHeight - button.topInset - button.bottomInset
            radius: Tokens.radius
            opacity: button.enabled ? 1 : 0.5
            color: Qt.tint(Kirigami.Theme.highlightColor, button.down ? Qt.rgba(0, 0, 0, 0.25)
                : button.hovered ? Qt.rgba(1, 1, 1, 0.12) : "transparent")
            border.width: Tokens.separatorWidth
            border.color: Qt.lighter(Kirigami.Theme.highlightColor, 1.25)
        }
    }
    Component {
        id: primaryLabel
        QQC2.Label {
            text: button.Kirigami.MnemonicData.richTextLabel
            textFormat: Text.StyledText
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            color: Kirigami.Theme.highlightedTextColor
            opacity: button.enabled ? 1 : 0.6
            elide: Text.ElideRight
        }
    }
    ButtonFocus { control: button }
}
