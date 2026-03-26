import QtQuick
import QtQuick.Controls
import HyprFM

ListView {
    id: root

    property var selectedIndices: []
    property int lastSelectedIndex: -1

    // Current directory path (used as drop target)
    property string currentPath: ""

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)

    clip: true

    focus: visible
    keyNavigationEnabled: false  // We handle keys manually

    function moveSelection(delta) {
        var current = selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1
        var next = Math.max(0, Math.min(count - 1, current + delta))
        if (next === current && current >= 0) return
        selectedIndices = [next]
        lastSelectedIndex = next
        positionViewAtIndex(next, ListView.Contain)
    }

    Keys.onUpPressed: moveSelection(-1)
    Keys.onDownPressed: moveSelection(1)

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
            var lo = Math.min(idx, lastSelectedIndex)
            var hi = Math.max(idx, lastSelectedIndex)
            var newSel = ctrl ? selectedIndices.slice() : []
            for (var i = lo; i <= hi; i++) {
                if (newSel.indexOf(i) < 0) newSel.push(i)
            }
            selectedIndices = newSel
        } else if (ctrl) {
            var newSel2 = selectedIndices.slice()
            var pos = newSel2.indexOf(idx)
            if (pos >= 0)
                newSel2.splice(pos, 1)
            else
                newSel2.push(idx)
            selectedIndices = newSel2
            lastSelectedIndex = idx
        } else {
            selectedIndices = [idx]
            lastSelectedIndex = idx
        }
    }

    function clearSelection() {
        selectedIndices = []
        lastSelectedIndex = -1
    }

    function selectAll() {
        var all = []
        for (var i = 0; i < count; i++) all.push(i)
        selectedIndices = all
    }

    delegate: Item {
        id: rowItem
        width: root.width
        height: 32

        required property int index
        required property string fileName
        required property string filePath
        required property string fileSizeText
        required property string fileModifiedText
        required property bool isDir
        required property string fileIconName

        readonly property bool isSelected: root.selectedIndices.indexOf(index) >= 0

        // ── Drag support ──────────────────────────────────────────────────
        Drag.active: false
        Drag.mimeData: ({ "text/uri-list": "file://" + rowItem.filePath })
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.dragType: Drag.Automatic

        Rectangle {
            anchors.fill: parent
            opacity: rowItem.Drag.active ? 0.5 : 1.0
            color: {
                if (rowItem.isSelected)
                    return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                if (rowMa.containsMouse)
                    return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                return "transparent"
            }
            border.color: rowItem.isSelected ? Theme.accent : "transparent"
            border.width: rowItem.isSelected ? 1 : 0

            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8

                // Icon
                Image {
                    width: 20
                    height: 20
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://icon/" + rowItem.fileIconName
                    sourceSize: Qt.size(20, 20)
                    asynchronous: true
                }

                // Name (fills remaining space)
                Text {
                    width: parent.width - 20 - 80 - 120 - parent.spacing * 3 - parent.anchors.leftMargin - parent.anchors.rightMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.fileName
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    elide: Text.ElideRight
                }

                // Size (80px, right-aligned)
                Text {
                    width: 80
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.fileSizeText
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSmall
                    horizontalAlignment: Text.AlignRight
                }

                // Date (120px, right-aligned)
                Text {
                    width: 120
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.fileModifiedText
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSmall
                    horizontalAlignment: Text.AlignRight
                }
            }

            MouseArea {
                id: rowMa
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        root.contextMenuRequested(
                            rowItem.filePath,
                            rowItem.isDir,
                            Qt.point(mouse.x, mouse.y + rowItem.y - root.contentY)
                        )
                        return
                    }
                    root.selectIndex(
                        rowItem.index,
                        mouse.modifiers & Qt.ControlModifier,
                        mouse.modifiers & Qt.ShiftModifier
                    )
                }

                onDoubleClicked: (mouse) => {
                    if (mouse.button !== Qt.LeftButton) return
                    root.fileActivated(rowItem.filePath, rowItem.isDir)
                }
            }
        }

        // Separator line
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
        }
    }

    // ── Drop area: accept files dropped onto this view ──────────────────────
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        z: -2

        onDropped: (drop) => {
            if (!root.currentPath) return
            var urls = drop.urls
            var paths = []
            for (var i = 0; i < urls.length; i++) {
                var s = urls[i].toString()
                if (s.startsWith("file://"))
                    paths.push(s.substring(7))
            }
            if (paths.length === 0) return
            if (drop.proposedAction === Qt.MoveAction)
                fileOps.moveFiles(paths, root.currentPath)
            else
                fileOps.copyFiles(paths, root.currentPath)
            drop.acceptProposedAction()
        }
    }

    // Click on empty area clears selection
    MouseArea {
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                root.contextMenuRequested("", false, Qt.point(mouse.x, mouse.y))
                return
            }
            root.clearSelection()
        }
    }
}
