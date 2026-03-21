import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

    visible: fileOps.busy
    height: visible ? 48 : 0
    color: Theme.crust

    Behavior on height {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: 4

        Text {
            Layout.fillWidth: true
            text: fileOps.statusText
            color: Theme.subtext
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideMiddle
        }

        ProgressBar {
            id: progressBar
            Layout.fillWidth: true
            value: fileOps.progress
            from: 0.0
            to: 1.0

            background: Rectangle {
                implicitHeight: 4
                color: Theme.surface
                radius: 2
            }

            contentItem: Item {
                implicitHeight: 4
                Rectangle {
                    width: progressBar.visualPosition * parent.width
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }
        }
    }
}
