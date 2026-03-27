import QtQuick
import QtQuick.Controls
import HyprFM

ListView {
    id: root

    property var selectedIndices: []
    property int lastSelectedIndex: -1   // anchor for shift-selection
    property int cursorIndex: -1         // moving end for keyboard navigation

    // Current directory path (used as drop target)
    property string currentPath: ""
    onCurrentPathChanged: clearSelection()

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)

    clip: true

    focus: visible
    keyNavigationEnabled: false  // We handle keys manually

    function moveSelection(delta, extend) {
        var current = cursorIndex >= 0 ? cursorIndex : (selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1)
        var next = Math.max(0, Math.min(count - 1, current + delta))
        if (next === current && current >= 0) return
        if (extend && lastSelectedIndex >= 0) {
            var lo = Math.min(next, lastSelectedIndex)
            var hi = Math.max(next, lastSelectedIndex)
            var newSel = []
            for (var i = lo; i <= hi; i++) newSel.push(i)
            selectedIndices = newSel
        } else {
            selectedIndices = [next]
            lastSelectedIndex = next
        }
        cursorIndex = next
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

        property bool dragStarted: false

        Rectangle {
            anchors.fill: parent
            opacity: rowItem.dragStarted ? 0.5 : 1.0
            color: {
                if (rowItem.isSelected)
                    return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                if (rowMa.containsMouse)
                    return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                return "transparent"
            }
            Behavior on color { ColorAnimation { duration: Theme.animDuration } }
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
                    font.pointSize: Theme.fontNormal
                    elide: Text.ElideRight
                }

                // Size (80px, right-aligned)
                Text {
                    width: 80
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.fileSizeText
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                    horizontalAlignment: Text.AlignRight
                }

                // Date (120px, right-aligned)
                Text {
                    width: 120
                    anchors.verticalCenter: parent.verticalCenter
                    text: rowItem.fileModifiedText
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                    horizontalAlignment: Text.AlignRight
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
                        if (!rowItem.isSelected)
                            root.selectIndex(rowItem.index, false, false)
                        var paths = root.selectedIndices.length > 1
                            ? root.selectedIndices.map(function(i) { return fsModel.filePath(i) })
                            : [rowItem.filePath]
                        rowItem.dragStarted = true
                        dragHelper.startDrag(paths, rowItem.fileIconName, paths.length)
                    }
                }

                onClicked: (mouse) => {
                    root.forceActiveFocus()
                    if (mouse.button === Qt.RightButton) {
                        var mapped = rowMa.mapToItem(null, mouse.x, mouse.y)
                        root.contextMenuRequested(
                            rowItem.filePath,
                            rowItem.isDir,
                            Qt.point(mapped.x, mapped.y)
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

                onReleased: { dragPending = false }
                onCanceled: { dragPending = false }

                Connections {
                    target: dragHelper
                    function onDragFinished() { rowItem.dragStarted = false }
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

    // ── Rubber-band selection + empty space clicks ───────────────────────────
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
            var idx = root.indexAt(mouse.x + root.contentX, mouse.y + root.contentY)
            if (idx >= 0) {
                mouse.accepted = false
                return
            }
            root.forceActiveFocus()
            if (mouse.button === Qt.LeftButton) {
                root.interactive = false
                dragStart = Qt.point(mouse.x, mouse.y)
                rubberBand.begin(dragStart)
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
                rubberBand.update(Qt.point(mouse.x, mouse.y))
                selectIntersecting()
            }
        }

        onReleased: {
            var wasRubberBand = rubberBandActive && rubberBand.visible
            rubberBand.end()
            rubberBandActive = false
            rubberBandJustFinished = wasRubberBand
            root.interactive = true
        }

        function selectIntersecting() {
            var rb = rubberBand.selectionRect
            if (rb.width < 4 && rb.height < 4) return

            var newSel = []
            var c = root.count
            for (var i = 0; i < c; i++) {
                var item = root.itemAtIndex(i)
                if (!item) continue
                var itemPos = root.mapFromItem(item, 0, 0)
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
        id: rubberBand
        anchors.fill: parent
        z: 11
    }
}
