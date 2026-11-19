import QtQuick
import QtQuick.Controls
import HyprFM
import Quill as Q

FocusScope {
    id: root
    Accessible.role: Accessible.Pane
    Accessible.name: "Miller columns view"

    property var fileModel: null
    // Only the search proxy has a searchQuery; other models read undefined.
    readonly property string highlightQuery:
        fileModel && fileModel.searchQuery !== undefined ? fileModel.searchQuery : ""
    property string currentPath: ""

    property var selectedIndices: currentColumn.selectedIndices
    property int lastSelectedIndex: currentColumn.lastSelectedIndex
    property int cursorIndex: currentColumn.cursorIndex

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)
    signal selectionChanged()
    signal interactionStarted()
    signal transferRequested(var paths, string destinationPath, bool moveOperation)

    function dropPaths(drop) {
        if (dragHelper.active && dragHelper.activePaths.length > 0)
            return dragHelper.activePaths.slice()

        var paths = []
        var urls = drop.urls || []

        for (var i = 0; i < urls.length; i++) {
            var s = urls[i].toString()
            paths.push(s.startsWith("file://") ? decodeURIComponent(s.substring(7)) : s)
        }

        if (paths.length === 0 && drop.hasText) {
            var lines = drop.text.split("\n")
            for (var j = 0; j < lines.length; j++) {
                var line = lines[j].trim()
                if (line !== "")
                    paths.push(line.startsWith("file://") ? decodeURIComponent(line.substring(7)) : line)
            }
        }

        return paths
    }

    // Track which folder in parent column leads to currentPath
    readonly property string parentPath: {
        if (!currentPath || currentPath === "/") return ""
        var parent = fileOps.parentPath(currentPath)
        return parent === currentPath ? "" : parent
    }

    readonly property string currentDirName: currentPath ? fileOps.displayNameForPath(currentPath) : ""

    property int rowHeight: 28

    // Column widths as fractions of the view (preview takes the rest).
    // Mirrored from config so both panes agree; dividers update them live
    // and save on release.
    property real parentFraction: config.millerFractions.parent
    property real currentFraction: config.millerFractions.current
    readonly property real minFraction: 0.12
    readonly property int parentColumnWidth: Math.floor(width * parentFraction)
    readonly property int currentColumnWidth: Math.floor(width * currentFraction) - 1
    Connections {
        target: config
        function onMillerFractionsChanged() {
            root.parentFraction = config.millerFractions.parent
            root.currentFraction = config.millerFractions.current
        }
    }

    // Draggable divider between two Miller columns.
    component ColumnDivider: Item {
        id: divider
        required property bool movesPreview   // false: parent|current, true: current|preview
        width: 1
        height: root.height
        z: 1   // above the neighbouring ListViews so the handle overhang wins

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
        }

        MouseArea {
            id: handle
            anchors.horizontalCenter: parent.horizontalCenter
            width: 10
            height: parent.height
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            preventStealing: true
            property real startX: 0
            property real startParent: 0
            property real startCurrent: 0

            Rectangle {
                anchors.centerIn: parent
                width: 2
                height: parent.height
                radius: 1
                color: Theme.accent
                opacity: handle.pressed ? 1 : (handle.containsMouse ? 0.7 : 0)
                Behavior on opacity { NumberAnimation { duration: Theme.animDurationFast } }
            }

            onPressed: (mouse) => {
                root.interactionStarted()
                startX = mapToItem(root, mouse.x, 0).x
                startParent = root.parentFraction
                startCurrent = root.currentFraction
            }
            onPositionChanged: (mouse) => {
                if (!pressed || root.width <= 0) return
                var d = (mapToItem(root, mouse.x, 0).x - startX) / root.width
                if (divider.movesPreview) {
                    root.currentFraction = Math.max(root.minFraction,
                        Math.min(startCurrent + d, 1 - root.parentFraction - root.minFraction))
                } else {
                    // parent|current line: preview keeps its width
                    var pair = startParent + startCurrent
                    root.parentFraction = Math.max(root.minFraction,
                        Math.min(startParent + d, pair - root.minFraction))
                    root.currentFraction = pair - root.parentFraction
                }
            }
            onReleased: config.saveMillerFractions(root.parentFraction, root.currentFraction)
        }
    }
    readonly property int minRowHeight: 22
    readonly property int maxRowHeight: 56
    readonly property int millerIconSize: Math.round(rowHeight * 0.571)  // 16 at default 28

    function syncParentModel() {
        if (!visible)
            return

        if (parentPath) {
            millerParentModel.setRootPath(parentPath)
        } else {
            millerParentModel.setRootPath("")
        }
    }

    onCurrentPathChanged: {
        syncParentModel()
        // See FileGridView/FileDetailedView: reset sticky drag state so
        // wheel scrolling works immediately after navigation without the
        // user needing to click an item first.
        if (currentColumn) currentColumn.interactive = true
        if (parentColumn) parentColumn.interactive = true
    }

    function selectAll() {
        currentColumn.selectAll()
    }

    function focusPath(path, reveal) {
        currentColumn.focusPath(path, reveal)
    }

    function clearSelection() {
        currentColumn.clearSelection()
    }

    function ensureCurrentColumnFocus() {
        currentColumn.forceActiveFocus()
        if (currentColumn.cursorIndex >= 0) {
            currentColumn.positionViewAtIndex(currentColumn.cursorIndex, ListView.Contain)
            return
        }
        if (currentColumn.selectedIndices.length > 0) {
            currentColumn.positionViewAtIndex(
                currentColumn.selectedIndices[currentColumn.selectedIndices.length - 1],
                ListView.Contain
            )
            return
        }
        if (currentColumn.count > 0) {
            currentColumn.selectIndex(0, false, false)
            currentColumn.positionViewAtIndex(0, ListView.Beginning)
        }
    }

    onVisibleChanged: {
        if (!visible)
            return
        syncParentModel()
        Qt.callLater(root.ensureCurrentColumnFocus)
    }

    Component.onCompleted: {
        if (visible)
            Qt.callLater(root.ensureCurrentColumnFocus)
    }

    function enterDirectory(dirPath) {
        root.fileActivated(dirPath, true)
    }

    function goUp() {
        if (parentPath) {
            // Remember current dir so we can highlight it after navigating up
            currentColumn.pendingFocusPath = currentPath
            currentColumn.pendingFocusReveal = true
            root.fileActivated(parentPath, true)
        }
    }

    function updatePreview() {
        if (!visible) {
            previewColumn.previewIsDir = false
            previewColumn.previewFilePath = ""
            return
        }

        var idx = currentColumn.cursorIndex >= 0 ? currentColumn.cursorIndex
            : (currentColumn.selectedIndices.length > 0 ? currentColumn.selectedIndices[currentColumn.selectedIndices.length - 1] : -1)
        if (idx < 0 || !fileModel) {
            previewColumn.previewIsDir = false
            previewColumn.previewFilePath = ""
            return
        }
        var fp = currentColumn.pathForRow(idx)
        var isDir = currentColumn.isDirForRow(idx)
        // previewIsDir first: assigning previewFilePath fires
        // onPreviewFilePathChanged, which reads previewIsDir to decide what
        // kind of preview to load. The other order hands it the previous
        // file's flag.
        previewColumn.previewIsDir = isDir
        previewColumn.previewFilePath = fp
    }

    Row {
        anchors.fill: parent

        // ── Parent column (20%) ───────────────────────────────────────────
        ListView {
            id: parentColumn
            objectName: "millerParentColumn"
            width: root.parentColumnWidth
            height: root.height
            clip: true
            reuseItems: true
            cacheBuffer: 512
            model: millerParentModel
            focus: false
            boundsBehavior: Flickable.StopAtBounds
            keyNavigationEnabled: false

            property bool revealScheduled: false

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            function pathForRow(row) {
                if (!millerParentModel || row < 0)
                    return ""
                if (millerParentModel.filePath)
                    return millerParentModel.filePath(row)
                return millerParentModel.data(millerParentModel.index(row, 0), 258) || ""
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

            function revealCurrentDir() {
                if (!root.parentPath || count <= 0)
                    return
                var idx = rowForPath(root.currentPath)
                if (idx >= 0)
                    positionViewAtIndex(idx, ListView.Contain)
            }

            function scheduleRevealCurrentDir() {
                if (revealScheduled)
                    return
                revealScheduled = true
                Qt.callLater(function() {
                    revealScheduled = false
                    revealCurrentDir()
                })
            }

            Connections {
                target: root

                function onCurrentPathChanged() {
                    parentColumn.scheduleRevealCurrentDir()
                }
            }

            Connections {
                target: millerParentModel
                ignoreUnknownSignals: true

                function onModelReset() {
                    parentColumn.scheduleRevealCurrentDir()
                }

                function onRowsInserted() {
                    parentColumn.scheduleRevealCurrentDir()
                }
            }

            // Right arrow from parent → focus middle column
            Keys.onRightPressed: (event) => {
                root.ensureCurrentColumnFocus()
                event.accepted = true
            }

            delegate: Item {
                id: parentDelegate
                width: parentColumn.width
                height: root.rowHeight

                required property int index
                required property string fileName
                required property string filePath
                required property bool isDir
                required property string fileIconName

                readonly property bool isCurrentDir: parentDelegate.fileName === root.currentDirName

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSmall
                    color: {
                        if (parentDelegate.isCurrentDir)
                            return parentMa.containsMouse
                                ? Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.95)
                                : Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.72)
                        if (parentMa.containsMouse)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                        if (parentDelegate.index % 2 === 1)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.025)
                        return "transparent"
                    }
                    border.color: "transparent"
                    border.width: 0

                    Rectangle {
                        visible: parentDelegate.isCurrentDir
                        width: 3
                        height: parent.height - 10
                        radius: width / 2
                        anchors.left: parent.left
                        anchors.leftMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.9)
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: parentDelegate.isCurrentDir ? 12 : 6
                        anchors.rightMargin: 4
                        spacing: 6

                        Image {
                            width: root.millerIconSize; height: root.millerIconSize
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://icon/" + parentDelegate.fileIconName + "?theme=" + config.iconTheme + "&r=" + config.iconRevision
                            sourceSize: Qt.size(root.millerIconSize * Screen.devicePixelRatio,
                                                root.millerIconSize * Screen.devicePixelRatio)
                            asynchronous: true
                            opacity: (parentDelegate.isCurrentDir ? 0.95 : 0.8)
                                     * (parentDelegate.fileName.startsWith(".") ? Theme.hiddenIconOpacity : 1)
                        }

                        Text {
                            width: parent.width - root.millerIconSize - parent.spacing - (parentDelegate.isDir ? root.millerIconSize : 0) - parent.anchors.leftMargin - parent.anchors.rightMargin
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: parentDelegate.fileName
                            color: parentDelegate.isCurrentDir ? Theme.text : Theme.subtext
                            font.pointSize: Theme.fontSmall
                            font.bold: parentDelegate.isCurrentDir
                            elide: Text.ElideRight
                        }

                        IconChevronRight {
                            visible: parentDelegate.isDir
                            size: root.millerIconSize
                            anchors.verticalCenter: parent.verticalCenter
                            color: parentDelegate.isCurrentDir
                                ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.75)
                                : Theme.subtext
                        }
                    }
                }

                MouseArea {
                    id: parentMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.interactionStarted()
                        parentColumn.forceActiveFocus()
                        if (parentDelegate.isDir) {
                            root.fileActivated(parentDelegate.filePath, true)
                        }
                    }
                }
            }
        }

        ColumnDivider { objectName: "millerDivider_left"; movesPreview: false }

        // ── Current column (50%) ─────────────────────────────────────────
        ListView {
            id: currentColumn
            objectName: "millerCurrentColumn"
            width: root.currentColumnWidth
            height: root.height
            clip: true
            reuseItems: true
            cacheBuffer: 512
            model: root.fileModel
            focus: root.visible
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            keyNavigationEnabled: false
            add: Transition {
                ParallelAnimation {
                    NumberAnimation {
                        properties: "opacity"
                        from: 0
                        to: 1
                        duration: Theme.animDurationFast
                        easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
                    }
                    NumberAnimation {
                        properties: "scale"
                        from: 0.98
                        to: 1
                        duration: Theme.animDuration
                        easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
                    }
                }
            }
            addDisplaced: Transition {
                NumberAnimation {
                    properties: "x,y"
                    duration: Theme.animDurationSlow
                    easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
                }
            }
            remove: Transition {
                ParallelAnimation {
                    NumberAnimation {
                        properties: "opacity"
                        to: 0
                        duration: Theme.animDurationFast
                        easing.type: Theme.animEasingExit; easing.bezierCurve: Theme.animBezierCurve
                    }
                    NumberAnimation {
                        properties: "scale"
                        to: 0.98
                        duration: Theme.animDurationFast
                        easing.type: Theme.animEasingExit; easing.bezierCurve: Theme.animBezierCurve
                    }
                }
            }
            removeDisplaced: Transition {
                NumberAnimation {
                    properties: "x,y"
                    duration: Theme.animDurationSlow
                    easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
                }
            }

            property var selectedIndices: []
            property int lastSelectedIndex: -1
            property int cursorIndex: -1
            property string typeAheadBuffer: ""

            Connections {
                target: root
                function onCurrentPathChanged() {
                    currentColumn.typeAheadBuffer = ""
                    typeAheadTimer.stop()
                    // If we have a pendingFocusPath (going up), don't clear — let focusPath handle it
                    if (currentColumn.pendingFocusPath === "") {
                        currentColumn.selectedIndices = []
                        currentColumn.lastSelectedIndex = -1
                        currentColumn.cursorIndex = -1
                        // Auto-select first item after model loads
                        currentColumn.autoSelectFirst = true
                    }
                    root.updatePreview()
                }
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Timer {
                id: typeAheadTimer
                interval: 1000
                repeat: false
                onTriggered: currentColumn.typeAheadBuffer = ""
            }

            function pathForRow(row) {
                if (!root.fileModel || row < 0) return ""
                if (root.fileModel.filePath) return root.fileModel.filePath(row)
                return root.fileModel.data(root.fileModel.index(row, 0), 258) || ""
            }

            function fileNameForRow(row) {
                if (!root.fileModel || row < 0) return ""
                if (root.fileModel.fileName) return root.fileModel.fileName(row)
                return root.fileModel.data(root.fileModel.index(row, 0), 257) || ""
            }

            function isDirForRow(row) {
                if (!root.fileModel || row < 0) return false
                if (root.fileModel.isDir) return root.fileModel.isDir(row)
                return root.fileModel.data(root.fileModel.index(row, 0), 265) || false
            }

            function rowForPath(path) {
                if (!path) return -1
                for (var i = 0; i < count; ++i) {
                    if (pathForRow(i) === path) return i
                }
                return -1
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
                    if (pos >= 0) newSel2.splice(pos, 1)
                    else newSel2.push(idx)
                    selectedIndices = newSel2
                    lastSelectedIndex = idx
                } else {
                    selectedIndices = [idx]
                    lastSelectedIndex = idx
                }
                cursorIndex = idx
                root.updatePreview()
                root.selectionChanged()
            }

            function clearSelection() {
                selectedIndices = []
                lastSelectedIndex = -1
                cursorIndex = -1
                root.updatePreview()
            }

            // Reconcile index-based selection/cursor when the model adds or
            // removes rows, so files moved out of this column stop carrying a
            // stale highlight.
            function reconcileAfterRemove(first, last) {
                var count = last - first + 1
                var newSel = []
                for (var i = 0; i < selectedIndices.length; ++i) {
                    var idx = selectedIndices[i]
                    if (idx < first) newSel.push(idx)
                    else if (idx > last) newSel.push(idx - count)
                }
                selectedIndices = newSel
                if (cursorIndex > last) cursorIndex -= count
                else if (cursorIndex >= first) cursorIndex = -1
                if (lastSelectedIndex > last) lastSelectedIndex -= count
                else if (lastSelectedIndex >= first) lastSelectedIndex = -1
            }

            function reconcileAfterInsert(first, last) {
                var count = last - first + 1
                var newSel = []
                for (var i = 0; i < selectedIndices.length; ++i) {
                    var idx = selectedIndices[i]
                    newSel.push(idx >= first ? idx + count : idx)
                }
                selectedIndices = newSel
                if (cursorIndex >= first) cursorIndex += count
                if (lastSelectedIndex >= first) lastSelectedIndex += count
            }

            function moveSelection(delta, extend) {
                if (count <= 0) return
                var current = cursorIndex >= 0 ? cursorIndex
                    : (selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1)
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
                root.updatePreview()
                root.selectionChanged()
            }

            function moveSelectionTo(index, extend) {
                if (count <= 0) return
                var next = Math.max(0, Math.min(count - 1, index))
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
                root.updatePreview()
                root.selectionChanged()
            }

            function selectAll() {
                var all = []
                for (var i = 0; i < count; i++) all.push(i)
                selectedIndices = all
                root.selectionChanged()
            }

            function activateCurrentSelection() {
                var idx = cursorIndex >= 0 ? cursorIndex
                    : (selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1)
                if (idx < 0) return
                var fp = pathForRow(idx)
                var isDir = isDirForRow(idx)
                if (isDir) {
                    root.enterDirectory(fp)
                } else {
                    root.fileActivated(fp, false)
                }
            }

            function isPrintableTypeAheadText(text) {
                return typeof text === "string" && text.length === 1 && /[^\x00-\x1f\x7f]/.test(text)
            }

            function findTypeAheadMatch(query, keepCurrentMatch) {
                if (!query || count <= 0) return -1
                var needle = query.toLocaleLowerCase()
                var current = cursorIndex >= 0 ? cursorIndex
                    : (selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1)
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
                    if (typeAheadBuffer.length === 0) return
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
                if (!isPrintableTypeAheadText(event.text)) return
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

            property string pendingFocusPath: ""
            property bool pendingFocusReveal: true
            property bool focusScheduled: false
            property bool autoSelectFirst: false

            function schedulePendingFocus() {
                if (focusScheduled) return
                focusScheduled = true
                Qt.callLater(function() {
                    focusScheduled = false
                    if (pendingFocusPath !== "") {
                        focusPath(pendingFocusPath, pendingFocusReveal)
                    } else if (autoSelectFirst && count > 0) {
                        autoSelectFirst = false
                        selectIndex(0, false, false)
                        positionViewAtIndex(0, ListView.Beginning)
                        forceActiveFocus()
                    }
                })
            }

            function focusPath(path, reveal) {
                if (!path || !root.fileModel) return false
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

            Connections {
                target: root.fileModel
                ignoreUnknownSignals: true
                function onModelReset() {
                    currentColumn.clearSelection()
                    currentColumn.schedulePendingFocus()
                }
                function onRowsInserted(_p, first, last) {
                    currentColumn.reconcileAfterInsert(first, last)
                    currentColumn.schedulePendingFocus()
                }
                function onRowsRemoved(_p, first, last) {
                    currentColumn.reconcileAfterRemove(first, last)
                }
            }

            Keys.onUpPressed: (event) => moveSelection(-1, event.modifiers & Qt.ShiftModifier)
            Keys.onDownPressed: (event) => moveSelection(1, event.modifiers & Qt.ShiftModifier)
            Keys.onRightPressed: (event) => {
                activateCurrentSelection()
                event.accepted = true
            }
            Keys.onLeftPressed: (event) => {
                root.goUp()
                event.accepted = true
            }
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Home) {
                    moveSelectionTo(0, event.modifiers & Qt.ShiftModifier)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_End) {
                    moveSelectionTo(count - 1, event.modifiers & Qt.ShiftModifier)
                    event.accepted = true
                    return
                }
                if (config.keyEventMatches("open", event.key, event.modifiers)) {
                    activateCurrentSelection()
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Escape) {
                    if (typeAheadBuffer.length > 0) {
                        typeAheadBuffer = ""
                        typeAheadTimer.stop()
                    } else if (selectedIndices.length > 0) {
                        clearSelection()
                    }
                    event.accepted = true
                    return
                }
                handleTypeAhead(event)
            }

            delegate: Item {
                id: currentDelegate
                width: currentColumn.width
                height: root.rowHeight

                // The remove transition leaves opacity/scale animated down, and
                // reuseItems recycles that item as-is (view transitions never
                // re-run on reuse) — without this reset the row comes back blank.
                ListView.onReused: { opacity = 1; scale = 1 }

                required property int index
                required property string fileName
                required property string filePath
                required property var fileModified
                required property string fileSizeText
                required property string fileModifiedText
                required property bool isDir
                required property string fileIconName
                required property string gitStatus
                required property string gitStatusIcon
                required property bool hasImagePreview
                required property bool hasVideoPreview

                readonly property bool isSelected: currentColumn.selectedIndices.indexOf(index) >= 0
                readonly property bool isCutPending: clipboard.isCut && clipboard.contains(currentDelegate.filePath)
                readonly property bool isPastePending: fileOps.pendingTargetPaths.indexOf(currentDelegate.filePath) >= 0

                property bool dragStarted: false

                DropArea {
                    id: folderDropArea
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    enabled: currentDelegate.isDir && !currentDelegate.isSelected

                    onDropped: (drop) => {
                        var paths = root.dropPaths(drop)
                        if (paths.length === 0) return
                        var dominated = paths.some(function(p) {
                            return currentDelegate.filePath === p || currentDelegate.filePath.startsWith(p + "/")
                        })
                        if (dominated) return
                        root.transferRequested(paths, currentDelegate.filePath, drop.proposedAction !== Qt.CopyAction)
                        drop.acceptProposedAction()
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSmall
                    opacity: currentDelegate.dragStarted ? 0.5 : 1.0
                    color: {
                        if (folderDropArea.containsDrag)
                            return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.3)
                        if (currentDelegate.isSelected)
                            return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                        if (currentDelegateMa.containsMouse)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                        if (currentDelegate.index % 2 === 1)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.025)
                        return "transparent"
                    }
                    border.color: folderDropArea.containsDrag ? Theme.accent : (currentDelegate.isSelected ? Theme.accent : "transparent")
                    border.width: folderDropArea.containsDrag ? 2 : (currentDelegate.isSelected ? 1 : 0)

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        spacing: 6

                        Item {
                            width: root.millerIconSize + 2; height: root.millerIconSize + 2
                            anchors.verticalCenter: parent.verticalCenter

                            readonly property bool hasThumbnail: !fileOps.isRemotePath(currentDelegate.filePath) && !fileOps.isSlowPath(currentDelegate.filePath)
                                && (currentDelegate.isDir || currentDelegate.hasImagePreview || currentDelegate.hasVideoPreview)

                            Image {
                                anchors.fill: parent
                                visible: !millerThumb.visible
                                opacity: currentDelegate.fileName.startsWith(".") ? Theme.hiddenIconOpacity : 1
                                source: "image://icon/" + currentDelegate.fileIconName + "?theme=" + config.iconTheme + "&r=" + config.iconRevision
                                sourceSize: Qt.size((root.millerIconSize + 2) * Screen.devicePixelRatio,
                                                    (root.millerIconSize + 2) * Screen.devicePixelRatio)
                                asynchronous: true
                            }

                            Image {
                                id: millerThumb
                                opacity: currentDelegate.fileName.startsWith(".") ? Theme.hiddenIconOpacity : 1
                                anchors.fill: parent
                                visible: parent.hasThumbnail && status === Image.Ready
                                         && implicitWidth > 0
                                fillMode: Image.PreserveAspectFit
                                source: parent.hasThumbnail
                                    ? ("image://thumbnail/" + encodeURIComponent(currentDelegate.filePath)
                                       + "?mtime=" + new Date(currentDelegate.fileModified).getTime()
                                       + (currentDelegate.isDir ? "&theme=" + config.iconTheme : ""))
                                    : ""
                                sourceSize: Qt.size(64 * Screen.devicePixelRatio, 64 * Screen.devicePixelRatio)
                                asynchronous: true
                            }

                            Rectangle {
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.topMargin: -3
                                anchors.rightMargin: -3
                                width: 12
                                height: 12
                                radius: 6
                                z: 2
                                color: Qt.rgba(Theme.mantle.r, Theme.mantle.g, Theme.mantle.b, 0.96)
                                border.width: 1
                                border.color: Qt.rgba(Theme.warning.r, Theme.warning.g, Theme.warning.b, 0.9)
                                opacity: currentDelegate.isCutPending ? 1 : 0
                                scale: currentDelegate.isCutPending ? 1 : 0.88
                                visible: opacity > 0

                                Behavior on opacity { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve } }
                                Behavior on scale { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve } }

                                // Built only while its badge shows; every delegate used to carry one,
                                // and the spinner painted its canvas even while hidden.
                                Loader {
                                    anchors.fill: parent
                                    active: parent.visible
                                    sourceComponent: Component {
                                        IconScissors {
                                            anchors.centerIn: parent
                                            size: 7
                                            color: Theme.warning
                                        }
                                    }
                                }
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: 16
                                height: 16
                                radius: 8
                                z: 3
                                color: Qt.rgba(Theme.mantle.r, Theme.mantle.g, Theme.mantle.b, 0.92)
                                border.width: 1
                                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
                                opacity: currentDelegate.isPastePending ? 1 : 0
                                scale: currentDelegate.isPastePending ? 1 : 0.9
                                visible: opacity > 0

                                Behavior on opacity { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve } }
                                Behavior on scale { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve } }

                                Loader {
                                    anchors.fill: parent
                                    active: parent.visible
                                    sourceComponent: Component {
                                        Q.Spinner {
                                            anchors.centerIn: parent
                                            size: "small"
                                            color: Theme.accent
                                            running: currentDelegate.isPastePending
                                            scale: 0.6
                                        }
                                    }
                                }
                            }

                            Loader {
                                active: currentDelegate.gitStatus !== ""
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                anchors.rightMargin: -3
                                anchors.bottomMargin: -3
                                width: 10; height: 10
                                sourceComponent: {
                                    switch (currentDelegate.gitStatusIcon) {
                                        case "git-modified":   return gitModifiedIcon
                                        case "git-staged":     return gitStagedIcon
                                        case "git-untracked":  return gitUntrackedIcon
                                        case "git-deleted":    return gitDeletedIcon
                                        case "git-renamed":    return gitRenamedIcon
                                        case "git-conflicted": return gitConflictedIcon
                                        case "git-ignored":    return gitIgnoredIcon
                                        case "git-dirty":      return gitDirtyIcon
                                        default: return null
                                    }
                                }
                            }
                        }

                        Text {
                            width: parent.width - (root.millerIconSize + 2) - (currentDelegate.isDir ? (root.millerIconSize + 2) : 0) - parent.spacing * (currentDelegate.isDir ? 2 : 1) - parent.anchors.leftMargin - parent.anchors.rightMargin
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: root.highlightQuery ? Text.StyledText : Text.PlainText
                            text: root.highlightQuery
                                ? Theme.highlightMatch(currentDelegate.fileName, root.highlightQuery)
                                : currentDelegate.fileName
                            color: Theme.text
                            font.pointSize: Theme.fontNormal
                            elide: Text.ElideRight
                        }

                        IconChevronRight {
                            visible: currentDelegate.isDir
                            size: root.millerIconSize + 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.subtext
                        }
                    }
                }

                MouseArea {
                    id: currentDelegateMa
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton

                    property point pressPos
                    property bool dragPending: false

                    onPressed: (mouse) => {
                        currentWheelScroller.stopAndSettle()
                        root.interactionStarted()
                        currentColumn.forceActiveFocus()
                        pressPos = Qt.point(mouse.x, mouse.y)
                        dragPending = (mouse.button === Qt.LeftButton)
                    }

                    onPositionChanged: (mouse) => {
                        if (!dragPending) return
                        var dx = mouse.x - pressPos.x
                        var dy = mouse.y - pressPos.y
                        if (Math.sqrt(dx*dx + dy*dy) > 10) {
                            dragPending = false
                            if (!currentDelegate.isSelected)
                                currentColumn.selectIndex(currentDelegate.index, false, false)
                            var paths = currentColumn.selectedIndices.length > 1
                                ? currentColumn.selectedIndices.map(function(i) { return currentColumn.pathForRow(i) })
                                : [currentDelegate.filePath]
                            currentDelegate.dragStarted = true
                            dragHelper.startDrag(paths, currentDelegate.fileIconName, paths.length)
                        }
                    }

                    onClicked: (mouse) => {
                        if (mouse.button === Qt.RightButton) {
                            var mapped = currentDelegateMa.mapToItem(null, mouse.x, mouse.y)
                            // See FileGridView: an unselected row is selected
                            // first so one right-click reaches the item menu;
                            // an existing multi-selection stays.
                            if (!currentDelegate.isSelected)
                                currentColumn.selectIndex(currentDelegate.index, false, false)
                            root.contextMenuRequested(
                                currentDelegate.filePath,
                                currentDelegate.isDir,
                                Qt.point(mapped.x, mapped.y)
                            )
                            return
                        }
                        currentColumn.selectIndex(
                            currentDelegate.index,
                            mouse.modifiers & Qt.ControlModifier,
                            mouse.modifiers & Qt.ShiftModifier
                        )
                    }

                    onDoubleClicked: (mouse) => {
                        if (mouse.button !== Qt.LeftButton) return
                        currentColumn.activateCurrentSelection()
                    }

                    onReleased: { dragPending = false }
                    onCanceled: { dragPending = false }

                    Connections {
                        target: dragHelper
                        function onDragFinished() { currentDelegate.dragStarted = false }
                    }
                }
            }

            DropArea {
                anchors.fill: parent
                keys: ["text/uri-list"]
                z: -2

                onDropped: (drop) => {
                    if (!root.currentPath) return
                    var paths = root.dropPaths(drop)
                    if (paths.length === 0) return
                    var allSameDir = paths.every(function(p) {
                        var parentDir = p.substring(0, p.lastIndexOf("/"))
                        return parentDir === root.currentPath
                    })
                    if (allSameDir) return
                    root.transferRequested(paths, root.currentPath, drop.proposedAction !== Qt.CopyAction)
                    drop.accept()
                }
            }

            // ── Rubber-band selection + empty space clicks ───────────────
            MouseArea {
                id: currentBgMa
                anchors.fill: parent
                z: 10
                preventStealing: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                property point dragStart
                property bool rubberBandActive: false
                property bool rubberBandJustFinished: false

                onWheel: (wheel) => {
                    if (wheel.modifiers & Qt.ControlModifier) {
                        currentWheelScroller.stopAndSettle()
                        root.interactionStarted()
                        var delta = currentWheelScroller.deltaFor(wheel)
                        if (delta === 0) {
                            wheel.accepted = false
                            return
                        }
                        var step = delta < 0 ? 2 : -2
                        root.rowHeight = Math.max(root.minRowHeight, Math.min(root.maxRowHeight, root.rowHeight + step))
                        wheel.accepted = true
                    } else {
                        wheel.accepted = false
                    }
                }

                onPressed: (mouse) => {
                    var idx = currentColumn.indexAt(mouse.x + currentColumn.contentX, mouse.y + currentColumn.contentY)
                    if (idx >= 0) {
                        mouse.accepted = false
                        return
                    }
                    currentWheelScroller.stopAndSettle()
                    root.interactionStarted()
                    currentColumn.forceActiveFocus()
                    if (mouse.button === Qt.LeftButton) {
                        currentColumn.interactive = false
                        dragStart = Qt.point(mouse.x, mouse.y)
                        currentRubberBand.begin(dragStart)
                        rubberBandActive = true
                    }
                }

                // Double-click on empty space goes up a directory (issue #14).
                onDoubleClicked: (mouse) => {
                    if (mouse.button === Qt.LeftButton)
                        root.goUp()
                }

                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        var mp = currentBgMa.mapToItem(null, mouse.x, mouse.y)
                        root.contextMenuRequested("", false, Qt.point(mp.x, mp.y))
                        return
                    }
                    if (rubberBandJustFinished) {
                        rubberBandJustFinished = false
                        return
                    }
                    currentColumn.clearSelection()
                }

                onPositionChanged: (mouse) => {
                    if (rubberBandActive) {
                        currentRubberBand.update(Qt.point(mouse.x, mouse.y))
                        selectIntersecting()
                    }
                }

                onReleased: {
                    var wasRubberBand = rubberBandActive && currentRubberBand.visible
                    currentRubberBand.end()
                    rubberBandActive = false
                    rubberBandJustFinished = wasRubberBand
                    currentColumn.interactive = true
                }

                function selectIntersecting() {
                    var rb = currentRubberBand.selectionRect
                    if (rb.width < 4 && rb.height < 4) return

                    var newSel = []
                    var c = currentColumn.count
                    for (var i = 0; i < c; i++) {
                        var item = currentColumn.itemAtIndex(i)
                        if (!item) continue
                        var itemPos = currentColumn.mapFromItem(item, 0, 0)
                        var itemRect = Qt.rect(itemPos.x, itemPos.y, item.width, item.height)
                        if (rectsIntersect(rb, itemRect))
                            newSel.push(i)
                    }
                    currentColumn.selectedIndices = newSel
                }

                function rectsIntersect(a, b) {
                    return a.x < b.x + b.width  &&
                           a.x + a.width  > b.x &&
                           a.y < b.y + b.height &&
                           a.y + a.height > b.y
                }
            }

            RubberBand {
                id: currentRubberBand
                anchors.fill: parent
                z: 11
            }

            KineticWheelScroller {
                id: currentWheelScroller
                anchors.fill: parent
                z: 12
                flickable: currentColumn
                wheelStep: 42
                mouseWheelMultiplier: 0.75
                touchpadMultiplier: 1.35
                minVelocity: 135
                maxVelocity: 3900
                kineticGain: 1.01
                onScrollStarted: root.interactionStarted()
            }
        }

        ColumnDivider { objectName: "millerDivider_right"; movesPreview: true }

        // ── Preview column (30%) ─────────────────────────────────────────
        FilePreviewPanel {
            id: previewColumn
            objectName: "millerPreviewColumn"
            width: root.width - root.parentColumnWidth - root.currentColumnWidth - 2
            height: root.height
            directoryModel: millerPreviewModel
            requester: "miller"
        }
    }

    Component { id: gitModifiedIcon;   IconGitModified   { size: 10 } }
    Component { id: gitStagedIcon;     IconGitStaged     { size: 10 } }
    Component { id: gitUntrackedIcon;  IconGitUntracked  { size: 10 } }
    Component { id: gitDeletedIcon;    IconGitDeleted    { size: 10 } }
    Component { id: gitRenamedIcon;    IconGitRenamed    { size: 10 } }
    Component { id: gitConflictedIcon; IconGitConflicted { size: 10 } }
    Component { id: gitIgnoredIcon;    IconGitIgnored    { size: 10 } }
    Component { id: gitDirtyIcon;      IconGitDirty      { size: 10 } }
}
