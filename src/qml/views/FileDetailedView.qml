import QtQuick
import QtQuick.Controls
import HyprFM

Item {
    id: root

    property var selectedIndices: []
    property int lastSelectedIndex: -1
    property string sortColumn: "name"
    property bool sortAscending: true

    // Current directory path (used as drop target)
    property string currentPath: ""

    // Exposed model/rootIndex bound by FileViewContainer
    property var viewModel
    property var viewRootIndex

    onViewModelChanged: listView.model = viewModel
    onViewRootIndexChanged: listView.rootIndex = viewRootIndex

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)
    signal sortRequested(string column, bool ascending)

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

    function clickHeader(col) {
        if (sortColumn === col) {
            sortAscending = !sortAscending
        } else {
            sortColumn = col
            sortAscending = true
        }
        root.sortRequested(sortColumn, sortAscending)
    }

    // Column widths
    readonly property int colName: root.width - 100 - 140 - 80 - 110
    readonly property int colSize: 100
    readonly property int colModified: 140
    readonly property int colType: 80
    readonly property int colPerms: 110

    Column {
        anchors.fill: parent

        // Header row
        Rectangle {
            width: root.width
            height: 28
            color: Theme.mantle

            Row {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 0

                Repeater {
                    model: [
                        { key: "name",     label: "Name",        width: root.colName },
                        { key: "size",     label: "Size",        width: root.colSize },
                        { key: "modified", label: "Modified",    width: root.colModified },
                        { key: "type",     label: "Type",        width: root.colType },
                        { key: "perms",    label: "Permissions", width: root.colPerms }
                    ]

                    delegate: Item {
                        width: modelData.width
                        height: 28

                        Rectangle {
                            anchors.fill: parent
                            color: hdrMa.containsMouse
                                ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                                : "transparent"
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 4
                            spacing: 3

                            Text {
                                text: modelData.label
                                color: root.sortColumn === modelData.key ? Theme.accent : Theme.subtext
                                font.pixelSize: Theme.fontSmall
                                font.bold: root.sortColumn === modelData.key
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                visible: root.sortColumn === modelData.key
                                text: root.sortAscending ? "▲" : "▼"
                                color: Theme.accent
                                font.pixelSize: 9
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        // Right border separator
                        Rectangle {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.topMargin: 4
                            anchors.bottomMargin: 4
                            width: 1
                            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
                        }

                        MouseArea {
                            id: hdrMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.clickHeader(modelData.key)
                        }
                    }
                }
            }

            // Bottom border
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
            }
        }

        // File list
        ListView {
            id: listView
            width: root.width
            height: root.height - 28
            clip: true

            // Elastic overscroll
            boundsMovement: Flickable.FollowBoundsBehavior
            boundsBehavior: Flickable.DragAndOvershootBounds
            flickDeceleration: 1500
            maximumFlickVelocity: 2500

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Item {
                id: detRow
                width: listView.width
                height: 28

                required property int index
                required property string fileName
                required property string filePath
                required property string fileSizeText
                required property string fileModifiedText
                required property string fileType
                required property int filePermissions
                required property bool isDir

                readonly property bool isSelected: root.selectedIndices.indexOf(index) >= 0

                // ── Drag support ──────────────────────────────────────────
                Drag.active: detDragHandler.active
                Drag.mimeData: ({ "text/uri-list": "file://" + detRow.filePath })
                Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
                Drag.dragType: Drag.Automatic

                // Compute unix-style permissions string from Qt permissions flags
                readonly property string permString: {
                    var p = filePermissions
                    // Qt::Permissions bit layout: owner r/w/x, group r/w/x, other r/w/x
                    // Qt uses QFile::Permission enum:
                    // ReadOwner=0x4000, WriteOwner=0x2000, ExeOwner=0x1000
                    // ReadGroup=0x0040, WriteGroup=0x0020, ExeGroup=0x0010
                    // ReadOther=0x0004, WriteOther=0x0002, ExeOther=0x0001
                    var s = isDir ? "d" : "-"
                    s += (p & 0x4000) ? "r" : "-"
                    s += (p & 0x2000) ? "w" : "-"
                    s += (p & 0x1000) ? "x" : "-"
                    s += (p & 0x0040) ? "r" : "-"
                    s += (p & 0x0020) ? "w" : "-"
                    s += (p & 0x0010) ? "x" : "-"
                    s += (p & 0x0004) ? "r" : "-"
                    s += (p & 0x0002) ? "w" : "-"
                    s += (p & 0x0001) ? "x" : "-"
                    return s
                }

                Rectangle {
                    anchors.fill: parent
                    opacity: detRow.Drag.active ? 0.5 : 1.0
                    color: {
                        if (detRow.isSelected)
                            return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                        if (rowMa.containsMouse)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                        // Alternating rows
                        if (detRow.index % 2 === 0)
                            return "transparent"
                        return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.025)
                    }
                    border.color: detRow.isSelected ? Theme.accent : "transparent"
                    border.width: detRow.isSelected ? 1 : 0

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 0

                        // Name
                        Row {
                            width: root.colName
                            height: parent.height
                            spacing: 6

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: detRow.isDir ? "📁" : "📄"
                                font.pixelSize: 14
                            }

                            Text {
                                width: root.colName - 20
                                anchors.verticalCenter: parent.verticalCenter
                                text: detRow.fileName
                                color: Theme.text
                                font.pixelSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }

                        // Size
                        Text {
                            width: root.colSize
                            anchors.verticalCenter: parent.verticalCenter
                            text: detRow.fileSizeText
                            color: Theme.subtext
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                            rightPadding: 8
                        }

                        // Modified
                        Text {
                            width: root.colModified
                            anchors.verticalCenter: parent.verticalCenter
                            text: detRow.fileModifiedText
                            color: Theme.subtext
                            font.pixelSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                            rightPadding: 8
                        }

                        // Type
                        Text {
                            width: root.colType
                            anchors.verticalCenter: parent.verticalCenter
                            text: detRow.fileType
                            color: Theme.subtext
                            font.pixelSize: Theme.fontSmall
                            elide: Text.ElideRight
                            rightPadding: 4
                        }

                        // Permissions (monospace)
                        Text {
                            width: root.colPerms
                            anchors.verticalCenter: parent.verticalCenter
                            text: detRow.permString
                            color: Theme.muted
                            font.pixelSize: Theme.fontSmall
                            font.family: "monospace"
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
                                    detRow.filePath,
                                    detRow.isDir,
                                    Qt.point(mouse.x, mouse.y + detRow.y - listView.contentY + 28)
                                )
                                return
                            }
                            root.selectIndex(
                                detRow.index,
                                mouse.modifiers & Qt.ControlModifier,
                                mouse.modifiers & Qt.ShiftModifier
                            )
                        }

                        onDoubleClicked: (mouse) => {
                            if (mouse.button !== Qt.LeftButton) return
                            root.fileActivated(detRow.filePath, detRow.isDir)
                        }
                    }
                }

                // DragHandler for initiating drag
                DragHandler {
                    id: detDragHandler
                    onActiveChanged: {
                        if (active) {
                            if (!detRow.isSelected)
                                root.selectIndex(detRow.index, false, false)
                            detRow.Drag.start()
                        } else {
                            detRow.Drag.drop()
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                }
            }

            // ── Drop area ─────────────────────────────────────────────────
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

            // Click empty area clears selection
            MouseArea {
                anchors.fill: parent
                z: -1
                onClicked: root.clearSelection()
            }
        }
    }
}
