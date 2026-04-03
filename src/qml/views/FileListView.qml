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
    onCurrentPathChanged: {
        clearSelection()
        pendingFocusPath = ""
        typeAheadBuffer = ""
        typeAheadTimer.stop()
    }

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)
    signal interactionStarted()

    property string pendingFocusPath: ""
    property bool pendingFocusReveal: true
    property bool focusScheduled: false
    property string typeAheadBuffer: ""

    clip: true

    focus: visible
    keyNavigationEnabled: false  // We handle keys manually
    boundsMovement: Flickable.StopAtBounds
    boundsBehavior: Flickable.StopAtBounds
    rebound: Transition {
        NumberAnimation {
            properties: "x,y"
            duration: Theme.animDurationSlow + 60
            easing.type: Easing.OutCubic
        }
    }

    function moveSelection(delta, extend) {
        wheelScroller.stopAndSettle()
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
    Keys.onPressed: (event) => handleTypeAhead(event)

    Timer {
        id: typeAheadTimer
        interval: 1000
        repeat: false
        onTriggered: root.typeAheadBuffer = ""
    }

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

    function pathForRow(row) {
        if (!model || row < 0)
            return ""

        if (model.filePath)
            return model.filePath(row)

        return model.data(model.index(row, 0), 258 /* FilePathRole */) || ""
    }

    function fileNameForRow(row) {
        if (!model || row < 0)
            return ""

        if (model.fileName)
            return model.fileName(row)

        return model.data(model.index(row, 0), 257 /* FileNameRole */) || ""
    }

    function rowForPath(path) {
        if (!path)
            return -1

        for (var i = 0; i < count; ++i) {
            if (pathForRow(i) === path)
                return i
        }

        return -1
    }

    function isPrintableTypeAheadText(text) {
        return typeof text === "string" && text.length === 1 && /[^\x00-\x1f\x7f]/.test(text)
    }

    function findTypeAheadMatch(query, keepCurrentMatch) {
        if (!query || count <= 0)
            return -1

        var needle = query.toLocaleLowerCase()
        var current = cursorIndex >= 0 ? cursorIndex : (selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1)
        if (keepCurrentMatch && current >= 0 && fileNameForRow(current).toLocaleLowerCase().startsWith(needle))
            return current

        for (var step = 1; step <= count; ++step) {
            var idx = current >= 0 ? (current + step) % count : step - 1
            if (fileNameForRow(idx).toLocaleLowerCase().startsWith(needle))
                return idx
        }

        return -1
    }

    function handleTypeAhead(event) {
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return

        if (event.key === Qt.Key_Backspace) {
            if (typeAheadBuffer.length === 0)
                return

            typeAheadBuffer = typeAheadBuffer.slice(0, -1)
            if (typeAheadBuffer.length > 0) {
                typeAheadTimer.restart()
                var backspaceMatch = findTypeAheadMatch(typeAheadBuffer, true)
                if (backspaceMatch >= 0) {
                    selectIndex(backspaceMatch, false, false)
                    positionViewAtIndex(backspaceMatch, ListView.Contain)
                }
            } else {
                typeAheadTimer.stop()
            }
            event.accepted = true
            return
        }

        if (event.key === Qt.Key_Escape && typeAheadBuffer.length > 0) {
            typeAheadBuffer = ""
            typeAheadTimer.stop()
            event.accepted = true
            return
        }

        if (!isPrintableTypeAheadText(event.text))
            return

        var nextBuffer = typeAheadBuffer + event.text
        var keepCurrentMatch = typeAheadBuffer.length > 0 && nextBuffer.startsWith(typeAheadBuffer)
        var match = findTypeAheadMatch(nextBuffer, keepCurrentMatch)
        if (match < 0) {
            nextBuffer = event.text
            match = findTypeAheadMatch(nextBuffer, false)
        }

        typeAheadBuffer = nextBuffer
        typeAheadTimer.restart()
        if (match >= 0) {
            selectIndex(match, false, false)
            positionViewAtIndex(match, ListView.Contain)
        }
        event.accepted = true
    }

    function schedulePendingFocus() {
        if (focusScheduled)
            return

        focusScheduled = true
        Qt.callLater(function() {
            focusScheduled = false
            if (pendingFocusPath !== "")
                focusPath(pendingFocusPath, pendingFocusReveal)
        })
    }

    function focusPath(path, reveal) {
        if (!path || !model)
            return false

        var idx = rowForPath(path)
        if (idx < 0) {
            pendingFocusPath = path
            pendingFocusReveal = (reveal !== false)
            return false
        }

        pendingFocusPath = ""
        pendingFocusReveal = true
        forceActiveFocus()
        selectIndex(idx, false, false)
        if (reveal !== false)
            positionViewAtIndex(idx, ListView.Contain)
        return true
    }

    function selectAll() {
        var all = []
        for (var i = 0; i < count; i++) all.push(i)
        selectedIndices = all
    }

    Connections {
        target: root.model
        ignoreUnknownSignals: true

        function onModelReset() {
            root.schedulePendingFocus()
        }

        function onRowsInserted() {
            root.schedulePendingFocus()
        }
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
                    wheelScroller.stopAndSettle()
                    root.interactionStarted()
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
                            ? root.selectedIndices.map(function(i) { return root.pathForRow(i) })
                            : [rowItem.filePath]
                        rowItem.dragStarted = true
                        dragHelper.startDrag(paths, rowItem.fileIconName, paths.length)
                    }
                }

                onClicked: (mouse) => {
                    root.forceActiveFocus()
                    if (mouse.button === Qt.RightButton) {
                        var mapped = rowMa.mapToItem(null, mouse.x, mouse.y)
                        if (rowItem.isSelected) {
                            root.contextMenuRequested(
                                rowItem.filePath,
                                rowItem.isDir,
                                Qt.point(mapped.x, mapped.y)
                            )
                        } else {
                            root.contextMenuRequested("", false, Qt.point(mapped.x, mapped.y))
                        }
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
                undoManager.moveFiles(paths, root.currentPath)
            else
                undoManager.copyFiles(paths, root.currentPath)
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
            wheelScroller.stopAndSettle()
            root.interactionStarted()
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

    KineticWheelScroller {
        id: wheelScroller
        anchors.fill: parent
        z: 12
        flickable: root
        onScrollStarted: root.interactionStarted()
    }
}
