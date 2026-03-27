import QtQuick
import QtQuick.Controls
import HyprFM

Item {
    id: root

    property var selectedIndices: []
    property int lastSelectedIndex: -1   // anchor for shift-selection
    property int cursorIndex: -1         // moving end for keyboard navigation
    property string sortColumn: "name"
    property bool sortAscending: true

    // Current directory path (used as drop target)
    property string currentPath: ""
    onCurrentPathChanged: clearSelection()

    // Model bound by FileViewContainer
    property var viewModel

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
        cursorIndex = idx
    }

    function clearSelection() {
        selectedIndices = []
        lastSelectedIndex = -1
        cursorIndex = -1
    }

    function selectAll() {
        var all = []
        for (var i = 0; i < listView.count; i++) all.push(i)
        selectedIndices = all
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
                            Behavior on color { ColorAnimation { duration: Theme.animDuration } }
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 4
                            spacing: 3

                            Text {
                                text: modelData.label
                                color: root.sortColumn === modelData.key ? Theme.accent : Theme.subtext
                                font.pointSize: Theme.fontSmall
                                font.bold: root.sortColumn === modelData.key
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            IconChevronDown {
                                visible: root.sortColumn === modelData.key
                                size: 12
                                color: Theme.accent
                                rotation: root.sortAscending ? 180 : 0
                                Behavior on rotation {
                                    NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                                }
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

            focus: visible
            keyNavigationEnabled: false

            function moveSelection(delta, extend) {
                var current = root.cursorIndex >= 0 ? root.cursorIndex : (root.selectedIndices.length > 0 ? root.selectedIndices[root.selectedIndices.length - 1] : -1)
                var next = Math.max(0, Math.min(count - 1, current + delta))
                if (next === current && current >= 0) return
                if (extend && root.lastSelectedIndex >= 0) {
                    var lo = Math.min(next, root.lastSelectedIndex)
                    var hi = Math.max(next, root.lastSelectedIndex)
                    var newSel = []
                    for (var i = lo; i <= hi; i++) newSel.push(i)
                    root.selectedIndices = newSel
                } else {
                    root.selectedIndices = [next]
                    root.lastSelectedIndex = next
                }
                root.cursorIndex = next
                positionViewAtIndex(next, ListView.Contain)
            }

            Keys.onUpPressed: (event) => moveSelection(-1, event.modifiers & Qt.ShiftModifier)
            Keys.onDownPressed: (event) => moveSelection(1, event.modifiers & Qt.ShiftModifier)

            // Elastic overscroll
            boundsMovement: Flickable.FollowBoundsBehavior
            boundsBehavior: Flickable.DragAndOvershootBounds
            flickDeceleration: 1500
            maximumFlickVelocity: 2500

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            model: root.viewModel

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
                required property string fileIconName

                readonly property bool isSelected: root.selectedIndices.indexOf(index) >= 0

                property bool dragStarted: false

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
                    opacity: detRow.dragStarted ? 0.5 : 1.0
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
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }
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

                            Image {
                                width: 16
                                height: 16
                                anchors.verticalCenter: parent.verticalCenter
                                source: "image://icon/" + detRow.fileIconName
                                sourceSize: Qt.size(16, 16)
                                asynchronous: true
                            }

                            Text {
                                width: root.colName - 20
                                anchors.verticalCenter: parent.verticalCenter
                                text: detRow.fileName
                                color: Theme.text
                                font.pointSize: Theme.fontSmall
                                elide: Text.ElideRight
                            }
                        }

                        // Size
                        Text {
                            width: root.colSize
                            anchors.verticalCenter: parent.verticalCenter
                            text: detRow.fileSizeText
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                            rightPadding: 8
                        }

                        // Modified
                        Text {
                            width: root.colModified
                            anchors.verticalCenter: parent.verticalCenter
                            text: detRow.fileModifiedText
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            horizontalAlignment: Text.AlignRight
                            rightPadding: 8
                        }

                        // Type
                        Text {
                            width: root.colType
                            anchors.verticalCenter: parent.verticalCenter
                            text: detRow.fileType
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideRight
                            rightPadding: 4
                        }

                        // Permissions (monospace)
                        Text {
                            width: root.colPerms
                            anchors.verticalCenter: parent.verticalCenter
                            text: detRow.permString
                            color: Theme.muted
                            font.pointSize: Theme.fontSmall
                            font.family: "monospace"
                        }
                    }

                    MouseArea {
                        id: rowMa
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton

                        property point pressPos
                        property bool dragPending: false

                        onPressed: (mouse) => {
                            pressPos = Qt.point(mouse.x, mouse.y)
                            dragPending = (mouse.button === Qt.LeftButton)
                        }

                        onPositionChanged: (mouse) => {
                            if (!dragPending) return
                            var dx = mouse.x - pressPos.x
                            var dy = mouse.y - pressPos.y
                            if (Math.sqrt(dx*dx + dy*dy) > 10) {
                                dragPending = false
                                if (!detRow.isSelected)
                                    root.selectIndex(detRow.index, false, false)
                                var paths = root.selectedIndices.length > 1
                                    ? root.selectedIndices.map(function(i) { return (searchProxy && searchProxy.searchActive ? searchProxy.filePath(i) : fsModel.filePath(i)) })
                                    : [detRow.filePath]
                                detRow.dragStarted = true
                                dragHelper.startDrag(paths, detRow.fileIconName, paths.length)
                            }
                        }

                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                var mapped = rowMa.mapToItem(null, mouse.x, mouse.y)
                                root.contextMenuRequested(
                                    detRow.filePath,
                                    detRow.isDir,
                                    Qt.point(mapped.x, mapped.y)
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

                        onReleased: { dragPending = false }
                        onCanceled: { dragPending = false }

                        Connections {
                            target: dragHelper
                            function onDragFinished() { detRow.dragStarted = false }
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

            // ── Rubber-band selection + empty space clicks ───────────────
            MouseArea {
                id: bgMa
                anchors.fill: parent
                z: 10
                preventStealing: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                property point dragStart
                property bool rubberBandActive: false
                property bool rubberBandJustFinished: false

                onPressed: (mouse) => {
                    var idx = listView.indexAt(mouse.x + listView.contentX, mouse.y + listView.contentY)
                    if (idx >= 0) {
                        mouse.accepted = false
                        return
                    }
                    root.forceActiveFocus()
                    if (mouse.button === Qt.LeftButton) {
                        listView.interactive = false
                        dragStart = Qt.point(mouse.x, mouse.y)
                        detailedRubberBand.begin(dragStart)
                        rubberBandActive = true
                    }
                }

                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        var mp = bgMa.mapToItem(null, mouse.x, mouse.y)
                        root.contextMenuRequested("", false, Qt.point(mp.x, mp.y))
                        return
                    }
                    if (rubberBandJustFinished) {
                        rubberBandJustFinished = false
                        return
                    }
                    root.clearSelection()
                }

                onPositionChanged: (mouse) => {
                    if (rubberBandActive) {
                        detailedRubberBand.update(Qt.point(mouse.x, mouse.y))
                        selectIntersecting()
                    }
                }

                onReleased: {
                    var wasRubberBand = rubberBandActive && detailedRubberBand.visible
                    detailedRubberBand.end()
                    rubberBandActive = false
                    rubberBandJustFinished = wasRubberBand
                    listView.interactive = true
                }

                function selectIntersecting() {
                    var rb = detailedRubberBand.selectionRect
                    if (rb.width < 4 && rb.height < 4) return

                    var newSel = []
                    var c = listView.count
                    for (var i = 0; i < c; i++) {
                        var item = listView.itemAtIndex(i)
                        if (!item) continue
                        var itemPos = listView.mapFromItem(item, 0, 0)
                        var itemRect = Qt.rect(itemPos.x, itemPos.y, item.width, item.height)
                        if (rectsIntersect(rb, itemRect))
                            newSel.push(i)
                    }
                    root.selectedIndices = newSel
                }

                function rectsIntersect(a, b) {
                    return a.x < b.x + b.width  &&
                           a.x + a.width  > b.x &&
                           a.y < b.y + b.height &&
                           a.y + a.height > b.y
                }
            }

            RubberBand {
                id: detailedRubberBand
                anchors.fill: parent
                z: 11
            }
        }
    }
}
