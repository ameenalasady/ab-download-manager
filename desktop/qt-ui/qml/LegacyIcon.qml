pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import org.kde.kirigami as Kirigami
import "LegacyPaths.js" as LegacyPaths

Item {
    id: root
    required property string name
    // Follows the desktop text color unless the caller passes a semantic color.
    property color color: Kirigami.Theme.textColor
    implicitWidth: 16
    implicitHeight: 16
    Repeater {
        model: LegacyPaths.paths[root.name] || LegacyPaths.paths.Folder
        delegate: Shape {
            id: shape
            required property var modelData
            width: 24; height: 24
            scale: Math.min(root.width, root.height) / 24
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeWidth: shape.modelData.stroked ? 2 : -1
                strokeColor: shape.modelData.stroked ? root.color : "transparent"
                fillColor: shape.modelData.stroked ? "transparent" : root.color
                fillRule: shape.modelData.evenOdd ? ShapePath.OddEvenFill : ShapePath.WindingFill
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: shape.modelData.d }
            }
        }
    }
}
