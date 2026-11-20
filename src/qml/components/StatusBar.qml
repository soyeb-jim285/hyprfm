import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import HyprFM
import Quill as Quill

Rectangle {
    id: statusBar
    Accessible.role: Accessible.StatusBar
    Accessible.name: {
        var parts = []
        if (selectedCount > 0) parts.push(selectedCount + " selected")
        else parts.push(itemSummary || itemCount + " items")
        if (folderCount > 0) parts.push(folderCount + " folders")
        if (selectedSize) parts.push(selectedSize)
        if (diskTotal > 0) {
            parts.push(Quill.Format.bytes(diskFree) + " free of "
                       + Quill.Format.bytes(diskTotal))
        }
        return parts.join(", ")
    }

    property int itemCount: 0
    property string itemSummary: ""
    property int folderCount: 0
    property int selectedCount: 0
    property string selectedSize: ""
    property bool selectedSizePending: false
    property string searchStatus: ""
    // -1 where the location has no filesystem worth reporting (trash, remote,
    // recents). Real numbers, not int: a disk is bigger than 2 GiB.
    property real diskFree: -1
    property real diskTotal: -1
    property bool isLoading: false

    // A local directory lists in a few milliseconds, so showing the spinner the
    // moment a load starts only flashes the item count off and straight back on
    // every navigation. Wait until a load is slow enough to be worth reporting.
    property bool showLoading: false

    Timer {
        id: loadingDelay
        interval: 300
        onTriggered: statusBar.showLoading = true
    }

    onIsLoadingChanged: {
        if (statusBar.isLoading) {
            loadingDelay.restart()
        } else {
            loadingDelay.stop()
            statusBar.showLoading = false
        }
    }

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

        RowLayout {
            Layout.fillWidth: true
            visible: statusBar.showLoading
            spacing: 6

            Rectangle {
                width: 12
                height: 12
                radius: 6
                color: "transparent"
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15)
                border.width: 2

                Rectangle {
                    width: 4
                    height: 4
                    x: 4; y: 0
                    radius: 2
                    color: Theme.accent
                }

                RotationAnimator on rotation {
                    from: 0
                    to: 360
                    duration: 1000
                    loops: Animation.Infinite
                    running: statusBar.showLoading
                }
            }

            Text {
                text: "Loading directory..."
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            Layout.fillWidth: true
            visible: !statusBar.showLoading
            text: {
                if (statusBar.itemSummary !== "") return statusBar.itemSummary
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

        // Disk usage, the way Dolphin shows it: the text sits on the bar
        // rather than beside it. The fill is deliberately low alpha so the
        // label stays readable across it in either theme. Wording and the
        // 75/90% colour steps match the sidebar's device bars.
        Rectangle {
            id: diskMeter
            objectName: "diskMeter"
            visible: statusBar.diskTotal > 0
            readonly property real usedFraction: statusBar.diskTotal > 0
                ? Math.max(0, Math.min(1, 1 - statusBar.diskFree / statusBar.diskTotal))
                : 0
            readonly property color fillColor: Theme.usageColor(usedFraction * 100)

            Layout.preferredWidth: diskLabel.implicitWidth + 20
            Layout.preferredHeight: 18
            Layout.alignment: Qt.AlignVCenter
            radius: Theme.radiusSmall
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
            clip: true

            Rectangle {
                objectName: "diskMeterFill"
                width: parent.width * diskMeter.usedFraction
                height: parent.height
                radius: parent.radius
                color: Qt.rgba(diskMeter.fillColor.r, diskMeter.fillColor.g,
                               diskMeter.fillColor.b, 0.30)
                Behavior on width { NumberAnimation { duration: Theme.animDuration } }
            }

            Text {
                id: diskLabel
                anchors.centerIn: parent
                text: Quill.Format.bytes(statusBar.diskFree)
                      + " free of " + Quill.Format.bytes(statusBar.diskTotal)
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
            }
        }
    }
}
