import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Rectangle {
    id: root

    property bool _showBar: false

    function transferIndexById(transferId) {
        for (var i = 0; i < transfersModel.count; ++i) {
            if (transfersModel.get(i).transferId === transferId)
                return i
        }
        return -1
    }

    function syncTransfers() {
        var snapshot = fileOps.activeTransfers
        var seen = ({})

        for (var i = 0; i < snapshot.length; ++i) {
            var transfer = snapshot[i]
            var transferId = transfer.id
            var key = String(transferId)
            var existingIndex = transferIndexById(transferId)
            var progress = transfer.progress

            seen[key] = true

            if (existingIndex >= 0) {
                var existing = transfersModel.get(existingIndex)
                if (progress >= 0 && existing.progress >= 0 && progress < existing.progress)
                    progress = existing.progress
            }

            if (existingIndex < 0) {
                transfersModel.insert(i, {
                    transferId: transferId,
                    statusText: transfer.statusText,
                    progress: progress,
                    speed: transfer.speed,
                    eta: transfer.eta,
                    currentFile: transfer.currentFile,
                    paused: transfer.paused
                })
                continue
            }

            if (existingIndex !== i) {
                transfersModel.move(existingIndex, i, 1)
                existingIndex = i
            }

            transfersModel.setProperty(existingIndex, "statusText", transfer.statusText)
            transfersModel.setProperty(existingIndex, "progress", progress)
            transfersModel.setProperty(existingIndex, "speed", transfer.speed)
            transfersModel.setProperty(existingIndex, "eta", transfer.eta)
            transfersModel.setProperty(existingIndex, "currentFile", transfer.currentFile)
            transfersModel.setProperty(existingIndex, "paused", transfer.paused)
        }

        for (var j = transfersModel.count - 1; j >= 0; --j) {
            if (!seen[String(transfersModel.get(j).transferId)])
                transfersModel.remove(j)
        }
    }

    visible: _showBar
    implicitHeight: _showBar ? contentCol.implicitHeight + 2 * Theme.spacing : 0
    color: Theme.crust

    ListModel {
        id: transfersModel
    }

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
            root.syncTransfers()
        }

        function onActiveTransfersChanged() {
            root.syncTransfers()
        }
    }

    Component.onCompleted: syncTransfers()

    ColumnLayout {
        id: contentCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing
        spacing: 8

        Repeater {
            model: transfersModel

            delegate: ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                required property int transferId
                required property string statusText
                required property real progress
                required property string speed
                required property string eta
                required property string currentFile
                required property bool paused
                required property int index

                // Row 1: status + speed
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: paused ? "Paused" : statusText
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                        elide: Text.ElideMiddle
                    }

                    Text {
                        visible: !paused && speed !== ""
                        text: speed
                        color: Theme.accent
                        font.pointSize: Theme.fontSmall
                    }
                }

                // Row 2: progress bar + percentage
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Q.ProgressBar {
                        Layout.fillWidth: true
                        value: progress <= 0 ? 0 : progress
                        indeterminate: progress < 0
                        monotonic: true
                    }

                    Text {
                        visible: progress >= 0
                        text: Math.round(progress * 100) + "%"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }
                }

                // Row 3: current file + ETA
                RowLayout {
                    visible: currentFile !== "" || eta !== ""
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        visible: currentFile !== ""
                        Layout.fillWidth: true
                        text: currentFile
                        color: Theme.muted
                        font.pointSize: Theme.fontSmall
                        elide: Text.ElideMiddle
                    }

                    Text {
                        visible: !paused && eta !== ""
                        text: "~" + eta
                        color: Theme.muted
                        font.pointSize: Theme.fontSmall
                    }
                }

                // Row 4: pause/cancel buttons
                Row {
                    Layout.alignment: Qt.AlignRight
                    spacing: 6

                    HoverRect {
                        width: 24; height: 24
                        onClicked: paused
                            ? fileOps.resumeTransfer(transferId)
                            : fileOps.pauseTransfer(transferId)

                        Loader {
                            anchors.centerIn: parent
                            sourceComponent: paused ? playIcon : pauseIcon
                        }
                    }

                    HoverRect {
                        width: 24; height: 24
                        onClicked: fileOps.cancelTransfer(transferId)

                        IconX {
                            anchors.centerIn: parent
                            size: 14
                            color: Theme.error
                        }
                    }
                }

                // Separator between transfers
                Rectangle {
                    visible: index < transfersModel.count - 1
                    Layout.fillWidth: true
                    height: 1
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                }
            }
        }

        // "Done" state when no active transfers but bar still lingering
        Text {
            visible: !fileOps.busy
            text: "Done"
            color: Theme.subtext
            font.pointSize: Theme.fontSmall
        }
    }

    Component { id: pauseIcon; IconPause { size: 14; color: Theme.subtext } }
    Component { id: playIcon; IconPlay { size: 14; color: Theme.accent } }
}
