import QtQuick
import QtQuick.Controls
import HyprFM

GridView {
    id: root

    property var selectedIndices: []
    property int lastSelectedIndex: -1

    // Current directory path (used as drop target)
    property string currentPath: ""

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)

    clip: true
    cellWidth: 110
    cellHeight: 110

    focus: visible
    keyNavigationEnabled: false

    readonly property int columnsPerRow: Math.max(1, Math.floor(width / cellWidth))

    function moveSelection(delta) {
        var current = selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1
        var next = Math.max(0, Math.min(count - 1, current + delta))
        if (next === current && current >= 0) return
        selectedIndices = [next]
        lastSelectedIndex = next
        positionViewAtIndex(next, GridView.Contain)
    }

    Keys.onLeftPressed: moveSelection(-1)
    Keys.onRightPressed: moveSelection(1)
    Keys.onUpPressed: moveSelection(-columnsPerRow)
    Keys.onDownPressed: moveSelection(columnsPerRow)

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

    // Parse file paths from a drop event (handles both internal and system DnD)
    function parseDragPaths(drop) {
        var paths = []

        // Try drop.urls first (system DnD)
        if (drop.urls && drop.urls.length > 0) {
            for (var i = 0; i < drop.urls.length; i++) {
                var s = drop.urls[i].toString()
                if (s.startsWith("file://"))
                    paths.push(s.substring(7))
            }
        }

        // Fallback: parse text/uri-list from mime data (internal DnD)
        if (paths.length === 0 && drop.hasText) {
            var text = drop.text
            var lines = text.split("\n")
            for (var j = 0; j < lines.length; j++) {
                var line = lines[j].trim()
                if (line.startsWith("file://"))
                    paths.push(line.substring(7))
            }
        }

        // Fallback: use our own stored drag data
        if (paths.length === 0 && root.dragMimeData["text/uri-list"]) {
            var uriList = root.dragMimeData["text/uri-list"]
            var uris = uriList.split("\n")
            for (var k = 0; k < uris.length; k++) {
                var uri = uris[k].trim()
                if (uri.startsWith("file://"))
                    paths.push(uri.substring(7))
            }
        }

        return paths
    }

    // ── Drag state ─────────────────────────────────────────────────────────
    property var dragMimeData: ({})
    property string dragIconName: ""
    property string dragFileName: ""
    property bool isDragging: false

    // Start a drag from a delegate
    function beginDrag(filePath, iconName, fileName, mouseX, mouseY) {
        // Build mime data from selection
        var paths = selectedIndices.length > 1
            ? selectedIndices.map(function(i) { return "file://" + fsModel.filePath(i) })
            : ["file://" + filePath]
        dragMimeData = { "text/uri-list": paths.join("\n") }
        dragIconName = iconName
        dragFileName = selectedIndices.length > 1
            ? (selectedIndices.length + " items")
            : fileName
        isDragging = true

        // Position the drag proxy at the mouse
        var globalPos = mapToGlobal(mouseX, mouseY)
        dragProxy.x = mapFromGlobal(globalPos.x, 0).x
        dragProxy.y = mapFromGlobal(0, globalPos.y).y
        dragProxy.Drag.active = true
    }

    function updateDrag(mouseX, mouseY) {
        dragProxy.x = mouseX
        dragProxy.y = mouseY
    }

    function endDrag() {
        if (isDragging) {
            dragProxy.Drag.drop()
            dragProxy.Drag.active = false
            isDragging = false
            interactive = true
            dragIconName = ""
            dragFileName = ""
        }
    }

    function cancelDrag() {
        if (isDragging) {
            dragProxy.Drag.active = false
            isDragging = false
            interactive = true
            dragIconName = ""
            dragFileName = ""
        }
    }

    // ── Drag proxy: invisible item that moves with the mouse ───────────────
    // DropAreas detect THIS item overlapping them
    Item {
        id: dragProxy
        width: 1
        height: 1
        z: 2000

        Drag.active: false
        Drag.keys: ["text/uri-list"]
        Drag.mimeData: root.dragMimeData
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.hotSpot.x: 0
        Drag.hotSpot.y: 0
    }

    // ── Drag ghost: visual feedback following the cursor ───────────────────
    Rectangle {
        id: dragGhost
        visible: root.isDragging
        width: 80
        height: 80
        radius: Theme.radiusMedium
        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.3)
        border.color: Theme.accent
        border.width: 1
        z: 1999
        opacity: 0.9
        x: dragProxy.x - 40
        y: dragProxy.y - 40

        Column {
            anchors.centerIn: parent
            spacing: 2

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 32
                height: 32
                source: root.dragIconName ? ("image://icon/" + root.dragIconName) : ""
                sourceSize: Qt.size(32, 32)
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.dragFileName
                color: Theme.text
                font.pixelSize: 10
                elide: Text.ElideMiddle
                width: 70
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    // ── Delegate ───────────────────────────────────────────────────────────
    delegate: Item {
        id: delegateItem
        width: root.cellWidth
        height: root.cellHeight

        required property int index
        required property string fileName
        required property string filePath
        required property bool isDir
        required property string fileIconName

        readonly property bool isSelected: root.selectedIndices.indexOf(index) >= 0

        // Per-folder drop target
        DropArea {
            id: folderDropArea
            anchors.fill: parent
            keys: ["text/uri-list"]
            enabled: delegateItem.isDir && root.isDragging && !delegateItem.isSelected

            onDropped: (drop) => {
                var paths = root.parseDragPaths(drop)
                if (paths.length === 0) return
                fileOps.moveFiles(paths, delegateItem.filePath)
                drop.accept()
            }
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: Theme.radiusMedium
            opacity: (root.isDragging && delegateItem.isSelected) ? 0.4 : 1.0
            color: {
                if (folderDropArea.containsDrag)
                    return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
                if (delegateItem.isSelected)
                    return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                if (ma.containsMouse)
                    return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                return "transparent"
            }
            border.color: folderDropArea.containsDrag ? Theme.accent
                        : delegateItem.isSelected ? Theme.accent : "transparent"
            border.width: folderDropArea.containsDrag ? 2
                        : delegateItem.isSelected ? 2 : 0

            Column {
                anchors.centerIn: parent
                spacing: 6

                readonly property bool isImage: !delegateItem.isDir &&
                    /\.(png|jpg|jpeg|gif|webp|bmp)$/i.test(delegateItem.filePath)

                Image {
                    visible: parent.isImage
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 48
                    height: 48
                    fillMode: Image.PreserveAspectFit
                    source: parent.isImage
                        ? ("image://thumbnail/" + delegateItem.filePath)
                        : ""
                    asynchronous: true
                }

                Image {
                    visible: !parent.isImage
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 48
                    height: 48
                    source: "image://icon/" + delegateItem.fileIconName
                    sourceSize: Qt.size(48, 48)
                    asynchronous: true
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

                property point pressPos
                property bool dragPending: false

                onPressed: (mouse) => {
                    pressPos = Qt.point(mouse.x, mouse.y)
                    dragPending = (mouse.button === Qt.LeftButton)
                    if (dragPending)
                        root.interactive = false  // Prevent Flickable from stealing grab
                }

                onPositionChanged: (mouse) => {
                    if (!dragPending && !root.isDragging) return
                    var dx = mouse.x - pressPos.x
                    var dy = mouse.y - pressPos.y
                    if (Math.sqrt(dx*dx + dy*dy) > 10) {
                        dragPending = false
                        if (!delegateItem.isSelected)
                            root.selectIndex(delegateItem.index, false, false)
                        // Map mouse to GridView coordinates
                        var mapped = ma.mapToItem(root, mouse.x, mouse.y)
                        root.beginDrag(
                            delegateItem.filePath,
                            delegateItem.fileIconName,
                            delegateItem.fileName,
                            mapped.x, mapped.y
                        )
                    }
                    if (root.isDragging) {
                        var mapped2 = ma.mapToItem(root, mouse.x, mouse.y)
                        root.updateDrag(mapped2.x, mapped2.y)
                    }
                }

                onClicked: (mouse) => {
                    if (!root.isDragging) root.interactive = true
                    root.forceActiveFocus()
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

                onReleased: {
                    dragPending = false
                    if (root.isDragging)
                        root.endDrag()
                    else
                        root.interactive = true
                }

                onCanceled: {
                    dragPending = false
                    root.cancelDrag()
                }
            }
        }
    }

    // ── Drop area: accept files dropped onto this view ───────────────────────
    DropArea {
        id: viewDropArea
        anchors.fill: parent
        keys: ["text/uri-list"]
        z: -2

        // Subtle background hint — only when not hovering a specific folder
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15)
            border.width: 1
            radius: Theme.radiusMedium
            visible: parent.containsDrag
        }

        onDropped: (drop) => {
            if (!root.currentPath) return
            var paths = root.parseDragPaths(drop)
            if (paths.length === 0) return
            fileOps.moveFiles(paths, root.currentPath)
            drop.accept()
        }
    }

    // ── Rubber-band selection ────────────────────────────────────────────────
    MouseArea {
        id: bgMa
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        property point dragStart

        onClicked: (mouse) => {
            root.forceActiveFocus()
            if (mouse.button === Qt.RightButton) {
                root.contextMenuRequested("", false, Qt.point(mouse.x, mouse.y))
                return
            }
            if (!rubberBand.visible)
                root.clearSelection()
        }

        onPressed: (mouse) => {
            if (mouse.button === Qt.LeftButton) {
                dragStart = Qt.point(mouse.x, mouse.y)
                rubberBand.begin(dragStart)
            }
        }

        onPositionChanged: (mouse) => {
            if (pressed) {
                rubberBand.update(Qt.point(mouse.x, mouse.y))
                selectIntersecting()
            }
        }

        onReleased: {
            rubberBand.end()
        }

        function selectIntersecting() {
            var rb = rubberBand.selectionRect
            if (rb.width < 4 && rb.height < 4) return

            var newSel = []
            var count = root.count
            for (var i = 0; i < count; i++) {
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
        z: 1
    }
}
