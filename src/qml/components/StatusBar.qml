import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import HyprFM

Rectangle {
    id: statusBar

    property int itemCount: 0
    property int folderCount: 0
    property int selectedCount: 0
    property string selectedSize: ""
    property bool selectedSizePending: false
    property string searchStatus: ""

    height: 28
    color: Theme.mantle
    clip: false

    // Inverse rounded corner — top left
    Shape {
        z: 1; width: Theme.radiusMedium; height: Theme.radiusMedium
        anchors.bottom: parent.top; anchors.left: parent.left
        ShapePath {
            fillColor: Theme.mantle; strokeColor: "transparent"
            startX: 0; startY: Theme.radiusMedium
            PathLine { x: Theme.radiusMedium; y: Theme.radiusMedium }
            PathArc {
                x: 0; y: 0
                radiusX: Theme.radiusMedium; radiusY: Theme.radiusMedium
                direction: PathArc.Clockwise
            }
            PathLine { x: 0; y: Theme.radiusMedium }
        }
    }

    // Inverse rounded corner — top right
    Shape {
        z: 1; width: Theme.radiusMedium; height: Theme.radiusMedium
        anchors.bottom: parent.top; anchors.right: parent.right
        ShapePath {
            fillColor: Theme.mantle; strokeColor: "transparent"
            startX: Theme.radiusMedium; startY: Theme.radiusMedium
            PathLine { x: 0; y: Theme.radiusMedium }
            PathArc {
                x: Theme.radiusMedium; y: 0
                radiusX: Theme.radiusMedium; radiusY: Theme.radiusMedium
                direction: PathArc.Counterclockwise
            }
            PathLine { x: Theme.radiusMedium; y: Theme.radiusMedium }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing

        Text {
            Layout.fillWidth: true
            text: {
                const files = statusBar.itemCount - statusBar.folderCount
                return statusBar.itemCount + " items (" + statusBar.folderCount + " folders, " + files + " files)"
            }
            color: Theme.subtext
            font.pointSize: Theme.fontSmall
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            visible: statusBar.selectedCount > 0
            text: statusBar.selectedCount + " selected" + (statusBar.selectedSize ? " \u2014 " + statusBar.selectedSize : "")
            color: statusBar.selectedSizePending ? Theme.accent : Theme.subtext
            font.pointSize: Theme.fontSmall
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            visible: statusBar.searchStatus !== ""
            text: statusBar.searchStatus
            color: Theme.accent
            font.pointSize: Theme.fontSmall
            verticalAlignment: Text.AlignVCenter
        }
    }
}
