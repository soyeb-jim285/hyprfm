import QtQuick
import HyprFM

Rectangle {
    id: root

    property alias hovered: hoverArea.containsMouse
    property alias cursorShape: hoverArea.cursorShape
    property bool hoverEnabled: true

    signal clicked()
    signal doubleClicked()

    color: hoverEnabled && hoverArea.containsMouse
        ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
        : "transparent"
    radius: Theme.radiusSmall

    Behavior on color { ColorAnimation { duration: Theme.animDuration } }

    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: root.hoverEnabled
        cursorShape: root.hoverEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        enabled: root.hoverEnabled
        onClicked: root.clicked()
        onDoubleClicked: root.doubleClicked()
    }
}
