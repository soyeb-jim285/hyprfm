import QtQuick
import QtQuick.Controls

Flickable {
    id: root
    property real zoomFactor: 1.0
    property alias source: image.source
    property alias sourceSize: image.sourceSize
    property alias status: image.status
    property alias fillMode: image.fillMode
    property alias asynchronous: image.asynchronous
    property alias smooth: image.smooth
    readonly property real paintedWidth: image.paintedWidth
    readonly property real paintedHeight: image.paintedHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    contentWidth: Math.max(width, image.paintedWidth)
    contentHeight: Math.max(height, image.paintedHeight)
    onSourceChanged: { contentX = 0; contentY = 0 }
    onZoomFactorChanged: {
        Qt.callLater(function() {
            root.contentX = Math.max(0, Math.min(root.contentX, root.contentWidth - root.width))
            root.contentY = Math.max(0, Math.min(root.contentY, root.contentHeight - root.height))
        })
    }
    TapHandler { onTapped: root.forceActiveFocus() }
    Image {
        id: image
        width: Math.max(1, root.width * root.zoomFactor)
        height: Math.max(1, root.height * root.zoomFactor)
        x: (root.contentWidth - width) / 2
        y: (root.contentHeight - height) / 2
        sourceSize: Qt.size(Math.min(4096, Math.ceil(width * Screen.devicePixelRatio)),
                            Math.min(4096, Math.ceil(height * Screen.devicePixelRatio)))
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
    }
    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
}
