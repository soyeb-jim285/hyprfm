import QtQuick
import QtQuick.Controls
import HyprFM

GridView {
    id: root

    property var selectedIndices: []
    property int lastSelectedIndex: -1

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)

    clip: true
    cellWidth: 110
    cellHeight: 110

    // Elastic overscroll
    boundsMovement: Flickable.FollowBoundsBehavior
    boundsBehavior: Flickable.DragAndOvershootBounds
    flickDeceleration: 1500
    maximumFlickVelocity: 2500

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    function selectIndex(idx, ctrl, shift) {
        if (shift && lastSelectedIndex >= 0) {
            // Range select
            var lo = Math.min(idx, lastSelectedIndex)
            var hi = Math.max(idx, lastSelectedIndex)
            var newSel = ctrl ? selectedIndices.slice() : []
            for (var i = lo; i <= hi; i++) {
                if (newSel.indexOf(i) < 0) newSel.push(i)
            }
            selectedIndices = newSel
        } else if (ctrl) {
            // Toggle
            var newSel2 = selectedIndices.slice()
            var pos = newSel2.indexOf(idx)
            if (pos >= 0)
                newSel2.splice(pos, 1)
            else
                newSel2.push(idx)
            selectedIndices = newSel2
            lastSelectedIndex = idx
        } else {
            // Single select
            selectedIndices = [idx]
            lastSelectedIndex = idx
        }
    }

    function clearSelection() {
        selectedIndices = []
        lastSelectedIndex = -1
    }

    delegate: Item {
        id: delegateItem
        width: root.cellWidth
        height: root.cellHeight

        required property int index
        required property string fileName
        required property string filePath
        required property bool isDir

        readonly property bool isSelected: root.selectedIndices.indexOf(index) >= 0

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: Theme.radiusMedium
            color: {
                if (delegateItem.isSelected)
                    return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                if (ma.containsMouse)
                    return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                return "transparent"
            }
            border.color: delegateItem.isSelected ? Theme.accent : "transparent"
            border.width: delegateItem.isSelected ? 2 : 0

            Column {
                anchors.centerIn: parent
                spacing: 6

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: delegateItem.isDir ? "📁" : "📄"
                    font.pixelSize: 36
                }

                Text {
                    width: root.cellWidth - 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: delegateItem.fileName
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    maximumLineCount: 2
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
            }

            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        root.contextMenuRequested(
                            delegateItem.filePath,
                            delegateItem.isDir,
                            Qt.point(mouse.x + delegateItem.x, mouse.y + delegateItem.y)
                        )
                        return
                    }
                    root.selectIndex(
                        delegateItem.index,
                        mouse.modifiers & Qt.ControlModifier,
                        mouse.modifiers & Qt.ShiftModifier
                    )
                }

                onDoubleClicked: (mouse) => {
                    if (mouse.button !== Qt.LeftButton) return
                    root.fileActivated(delegateItem.filePath, delegateItem.isDir)
                }
            }
        }
    }

    // Click on empty area clears selection
    MouseArea {
        anchors.fill: parent
        z: -1
        onClicked: root.clearSelection()
    }
}
