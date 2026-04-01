import QtQuick
import QtQuick.Controls
import HyprFM

GridView {
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
    // Zoom controls column count, not icon size
    property int columnCount: 7
    readonly property int minColumns: 3
    readonly property int maxColumns: 12
    readonly property int labelHeight: 32  // two lines of text below icon

    cellWidth: Math.floor(width / columnCount)
    cellHeight: cellWidth  // square cells
    readonly property int iconSize: cellHeight - 8 - labelHeight - 5  // 8px top, 0px gap, 5px bottom

    focus: visible
    keyNavigationEnabled: false

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
        positionViewAtIndex(next, GridView.Contain)
    }

    Keys.onLeftPressed: (event) => moveSelection(-1, event.modifiers & Qt.ShiftModifier)
    Keys.onRightPressed: (event) => moveSelection(1, event.modifiers & Qt.ShiftModifier)
    Keys.onUpPressed: (event) => moveSelection(-columnCount, event.modifiers & Qt.ShiftModifier)
    Keys.onDownPressed: (event) => moveSelection(columnCount, event.modifiers & Qt.ShiftModifier)

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

    // Parse file paths from a drop event
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

        // Fallback: parse text/uri-list from text mime data
        if (paths.length === 0 && drop.hasText) {
            var text = drop.text
            var lines = text.split("\n")
            for (var j = 0; j < lines.length; j++) {
                var line = lines[j].trim()
                if (line.startsWith("file://"))
                    paths.push(line.substring(7))
            }
        }

        return paths
    }

    // ── Drag state ─────────────────────────────────────────────────────────
    property string dragIconName: ""
    property string dragFileName: ""
    property bool isDragging: false

    // Start a drag from a delegate — uses C++ QDrag for system-wide DnD
    function beginDrag(filePath, iconName, fileName, mouseX, mouseY) {
        var paths = selectedIndices.length > 1
            ? selectedIndices.map(function(i) { return (searchProxy && searchProxy.searchActive ? searchProxy.filePath(i) : fsModel.filePath(i)) })
            : [filePath]
        dragIconName = iconName
        dragFileName = selectedIndices.length > 1
            ? (selectedIndices.length + " items")
            : fileName
        isDragging = true

        dragHelper.startDrag(paths, iconName, paths.length)
    }

    function updateDrag(mouseX, mouseY) {
        // System drag handles cursor tracking
    }

    function endDrag() {
        if (isDragging) {
            isDragging = false
            interactive = true
            dragIconName = ""
            dragFileName = ""
        }
    }

    function cancelDrag() {
        if (isDragging) {
            isDragging = false
            interactive = true
            dragIconName = ""
            dragFileName = ""
        }
    }

    Connections {
        target: dragHelper
        function onDragFinished() { root.endDrag() }
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
            enabled: delegateItem.isDir && !delegateItem.isSelected

            onDropped: (drop) => {
                var paths = root.parseDragPaths(drop)
                if (paths.length === 0) return
                // Don't move into itself or its own parent
                var dominated = paths.some(function(p) {
                    return delegateItem.filePath === p || delegateItem.filePath.startsWith(p + "/")
                })
                if (dominated) return
                fileOps.moveFiles(paths, delegateItem.filePath)
                drop.accept()
            }
        }

        readonly property bool isImage: !delegateItem.isDir &&
            /\.(png|jpg|jpeg|gif|webp|bmp)$/i.test(delegateItem.filePath)
        readonly property bool isVideo: !delegateItem.isDir &&
            /\.(mp4|mkv|avi|mov|wmv|flv|webm|m4v|mpg|mpeg|3gp|ts)$/i.test(delegateItem.filePath)
        readonly property bool hasThumbnail: isImage || isVideo

        Image {
            id: thumbImg
            visible: delegateItem.hasThumbnail
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 8
            width: root.iconSize
            height: root.iconSize
            fillMode: Image.PreserveAspectFit
            source: delegateItem.hasThumbnail
                ? ("image://thumbnail/" + delegateItem.filePath)
                : ""
            sourceSize: Qt.size(root.iconSize * Screen.devicePixelRatio,
                                root.iconSize * Screen.devicePixelRatio)
            asynchronous: true
        }

        Image {
            id: iconImg
            visible: !delegateItem.hasThumbnail
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 8
            width: root.iconSize
            height: root.iconSize
            source: "image://icon/" + delegateItem.fileIconName
            sourceSize: Qt.size(root.iconSize, root.iconSize)
            asynchronous: true
        }

        // Hidden text to check if name fits in 2 lines
        Text {
            id: measureText
            visible: false
            width: labelText.width
            font: labelText.font
            text: delegateItem.fileName
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 3
        }

        Text {
            id: labelText
            width: parent.width - 12
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: (iconImg.visible ? iconImg : thumbImg).bottom
            anchors.topMargin: 0
            text: {
                var name = delegateItem.fileName
                // If it fits in 2 lines, show as-is
                if (measureText.lineCount <= 2) return name
                // Middle-elide: keep last 6 chars (extension + some context)
                var keep = Math.min(6, Math.floor(name.length / 4))
                var maxFront = name.length - keep - 3
                // Approximate: 2 lines worth of chars
                var charsPerLine = Math.floor(labelText.width / (labelText.font.pixelSize * 0.55))
                var frontChars = Math.min(maxFront, charsPerLine * 2 - keep - 3)
                if (frontChars < 1) frontChars = 1
                return name.substring(0, frontChars) + "\u2026" + name.substring(name.length - keep)
            }
            color: Theme.text
            font.pointSize: Theme.fontSmall
            horizontalAlignment: Text.AlignHCenter
            maximumLineCount: 2
            wrapMode: Text.WrapAnywhere
            clip: true
            height: root.labelHeight
        }


        // Selection/hover rect wraps tightly around the content
        Rectangle {
            id: selectionRect
            anchors.fill: parent
            anchors.margins: 0
            radius: Theme.radiusMedium
            z: -1
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
            Behavior on color { ColorAnimation { duration: Theme.animDuration } }
            border.color: folderDropArea.containsDrag ? Theme.accent
                        : delegateItem.isSelected ? Theme.accent : "transparent"
            border.width: folderDropArea.containsDrag ? 2
                        : delegateItem.isSelected ? 2 : 0
        }

        MouseArea {
            id: ma
            anchors.fill: selectionRect
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
                    var mapped = ma.mapToItem(null, mouse.x, mouse.y)
                    root.contextMenuRequested(
                        delegateItem.filePath,
                        delegateItem.isDir,
                        Qt.point(mapped.x, mapped.y)
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
            // Don't move files into the directory they're already in
            var allSameDir = paths.every(function(p) {
                var parentDir = p.substring(0, p.lastIndexOf("/"))
                return parentDir === root.currentPath
            })
            if (allSameDir) return
            fileOps.moveFiles(paths, root.currentPath)
            drop.accept()
        }
    }

    // ── Rubber-band selection + empty space clicks ───────────────────────────
    // z:10 so it receives presses BEFORE the Flickable can steal them
    MouseArea {
        id: bgMa
        anchors.fill: parent
        z: 10
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton

        property point dragStart
        property bool rubberBandActive: false
        property bool rubberBandJustFinished: false

        onWheel: (wheel) => {
            if (wheel.modifiers & Qt.ControlModifier) {
                // Scroll up = zoom in = fewer columns (bigger icons)
                var step = wheel.angleDelta.y > 0 ? -1 : 1
                root.columnCount = Math.max(root.minColumns, Math.min(root.maxColumns, root.columnCount + step))
                wheel.accepted = true
            } else {
                wheel.accepted = false
            }
        }

        onPressed: (mouse) => {
            var idx = root.indexAt(mouse.x + root.contentX, mouse.y + root.contentY)
            if (idx >= 0) {
                mouse.accepted = false
                return
            }
            // Empty space (between cells or outside content rects)
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
