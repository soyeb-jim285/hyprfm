import QtQuick
import HyprFM

Image {
    id: root
    property real size: 24
    property color color: Theme.accent
    property real strokeWidth: Math.max(1, size / 12)
    property string iconName: ""
    width: size
    height: size
    sourceSize: Qt.size(Math.ceil(size * Screen.devicePixelRatio), Math.ceil(size * Screen.devicePixelRatio))
    readonly property color effectiveColor: config.iconTheme === "Strata"
        && (color === Theme.text || color === Theme.subtext || color === Theme.muted) ? Theme.accent : color
    source: Theme.fileIconSource(iconName, "", size, false) + "&color=" + encodeURIComponent(effectiveColor)
        + "&strokeScale=" + (strokeWidth / Math.max(1, size / 12))
    asynchronous: false
}
