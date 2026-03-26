import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property real size: 24
    property color color: "#ffffff"
    width: size; height: size

    Image {
        id: themeIcon
        anchors.fill: parent
        source: config.builtinIcons ? "" : ("image://icon/go-previous-symbolic?color=" + root.color)
        sourceSize: Qt.size(root.size, root.size)
        visible: !config.builtinIcons && status === Image.Ready && sourceSize.width > 0
    }

    Shape {
        anchors.fill: parent
        visible: config.builtinIcons || themeIcon.status !== Image.Ready || themeIcon.sourceSize.width <= 0
        layer.enabled: visible; layer.smooth: true

        ShapePath {
            strokeColor: root.color; strokeWidth: Math.max(1, root.size / 12)
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap; joinStyle: ShapePath.RoundJoin
            scale: Qt.size(root.size / 24, root.size / 24)
            PathSvg { path: "m15 18-6-6 6-6" }
        }
    }
}
