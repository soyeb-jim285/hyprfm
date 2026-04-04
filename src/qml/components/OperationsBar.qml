import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Rectangle {
    id: root

    property bool _showBar: false

    visible: _showBar
    implicitHeight: _showBar ? contentCol.implicitHeight + 2 * Theme.spacing : 0
    color: Theme.crust

    Behavior on implicitHeight {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }

    Timer {
        id: hideTimer
        interval: 1500
        onTriggered: root._showBar = false
    }

    Connections {
        target: fileOps
        function onBusyChanged() {
            if (fileOps.busy) {
                hideTimer.stop()
                root._showBar = true
            } else {
                hideTimer.restart()
            }
        }
    }

    ColumnLayout {
        id: contentCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 4

            Text {
                Layout.fillWidth: true
                text: {
                    if (!fileOps.busy) return "Done"
                    if (fileOps.paused) return "Paused"
                    return fileOps.statusText
                }
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                elide: Text.ElideMiddle
            }

            Text {
                visible: fileOps.busy && !fileOps.paused && fileOps.speed !== ""
                text: fileOps.speed
                color: Theme.accent
                font.pointSize: Theme.fontSmall
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Q.ProgressBar {
                Layout.fillWidth: true
                value: fileOps.busy ? Math.max(0, fileOps.progress) : 1.0
                indeterminate: fileOps.busy && fileOps.progress < 0
            }

            Text {
                visible: fileOps.busy && fileOps.progress >= 0
                text: Math.round(fileOps.progress * 100) + "%"
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
            }
        }

        RowLayout {
            visible: fileOps.busy && (fileOps.currentFile !== "" || fileOps.eta !== "")
            Layout.fillWidth: true
            spacing: 4

            Text {
                visible: fileOps.currentFile !== ""
                Layout.fillWidth: true
                text: fileOps.currentFile
                color: Theme.muted
                font.pointSize: Theme.fontSmall
                elide: Text.ElideMiddle
            }

            Text {
                visible: !fileOps.paused && fileOps.eta !== ""
                text: "~" + fileOps.eta
                color: Theme.muted
                font.pointSize: Theme.fontSmall
            }
        }

        Row {
            visible: fileOps.busy
            Layout.alignment: Qt.AlignRight
            spacing: 6

            HoverRect {
                width: 24; height: 24
                onClicked: fileOps.paused ? fileOps.resumeTransfer() : fileOps.pauseTransfer()

                Loader {
                    anchors.centerIn: parent
                    sourceComponent: fileOps.paused ? playIcon : pauseIcon
                }
            }

            HoverRect {
                width: 24; height: 24
                onClicked: fileOps.cancelTransfer()

                IconX {
                    anchors.centerIn: parent
                    size: 14
                    color: Theme.error
                }
            }
        }
    }

    Component { id: pauseIcon; IconPause { size: 14; color: Theme.subtext } }
    Component { id: playIcon; IconPlay { size: 14; color: Theme.accent } }
}
