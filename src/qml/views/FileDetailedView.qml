import QtQuick
import QtQuick.Controls
import HyprFM

FocusScope {
    id: root
    Accessible.role: Accessible.Table
    Accessible.name: "File details"
    focus: visible

    property var selectedIndices: []
    property int lastSelectedIndex: -1   // anchor for shift-selection
    property int cursorIndex: -1         // moving end for keyboard navigation
    property string sortColumn: "name"
    property bool sortAscending: true

    // Current directory path (used as drop target)
    property string currentPath: ""
    onCurrentPathChanged: {
        clearSelection()
        pendingFocusPath = ""
        typeAheadBuffer = ""
        typeAheadTimer.stop()
        Qt.callLater(refreshFolderItemCounts)
    }

    // Model bound by FileViewContainer
    property var viewModel
    property string pendingFocusPath: ""
    property bool pendingFocusReveal: true
    property bool focusScheduled: false
    property string typeAheadBuffer: ""

    property int rowHeight: 28
    readonly property int minRowHeight: 22
    readonly property int maxRowHeight: 56
    readonly property int detailIconSize: Math.round(rowHeight * 0.571)  // 16 at default 28

    // Map of folder path → item count
    property var folderItemCounts: ({})

    function refreshFolderItemCounts() {
        folderItemCounts = ({})
        if (!viewModel || listView.count <= 0 || !viewModel.folderItemCounts)
            return
        var paths = []
        for (var i = 0; i < listView.count; ++i) {
            if (isDirForRow(i)) {
                var p = pathForRow(i)
                if (p && !fileOps.isRemotePath(p))
                    paths.push(p)
            }
        }
        if (paths.length === 0)
            return
        folderItemCounts = viewModel.folderItemCounts(paths)
    }

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)
    signal sortRequested(string column, bool ascending)
    signal interactionStarted()
    signal transferRequested(var paths, string destinationPath, bool moveOperation)

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
        if (!viewModel || row < 0)
            return ""

        if (viewModel.filePath)
            return viewModel.filePath(row)

        return viewModel.data(viewModel.index(row, 0), 258 /* FilePathRole */) || ""
    }

    function fileNameForRow(row) {
        if (!viewModel || row < 0)
            return ""

        if (viewModel.fileName)
            return viewModel.fileName(row)

        return viewModel.data(viewModel.index(row, 0), 257 /* FileNameRole */) || ""
    }

    function isDirForRow(row) {
        if (!viewModel || row < 0)
            return false

        if (viewModel.isDir)
            return viewModel.isDir(row)

        return viewModel.data(viewModel.index(row, 0), 265 /* IsDirRole */) || false
    }

    function rowForPath(path) {
        if (!path)
            return -1

        for (var i = 0; i < listView.count; ++i) {
            if (pathForRow(i) === path)
                return i
        }

        return -1
    }

    function isPrintableTypeAheadText(text) {
        return typeof text === "string" && text.length === 1 && /[^\x00-\x1f\x7f]/.test(text)
    }

    function findTypeAheadMatch(query, keepCurrentMatch) {
        if (!query || listView.count <= 0)
            return -1

        var needle = query.toLocaleLowerCase()
        var current = cursorIndex >= 0 ? cursorIndex : (selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1)
        if (keepCurrentMatch && current >= 0 && fileNameForRow(current).toLocaleLowerCase().startsWith(needle))
            return current

        for (var step = 1; step <= listView.count; ++step) {
            var idx = current >= 0 ? (current + step) % listView.count : step - 1
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
                    listView.positionViewAtIndex(backspaceMatch, ListView.Contain)
                }
            } else {
                typeAheadTimer.stop()
            }
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
            listView.positionViewAtIndex(match, ListView.Contain)
        }
        event.accepted = true
    }

    function activateCurrentSelection() {
        var idx = cursorIndex >= 0 ? cursorIndex : (selectedIndices.length > 0 ? selectedIndices[selectedIndices.length - 1] : -1)
        if (idx < 0)
            return

        root.fileActivated(pathForRow(idx), isDirForRow(idx))
    }

    function moveSelectionTo(index, extend) {
        wheelScroller.stopAndSettle()
        if (listView.count <= 0)
            return

        var next = Math.max(0, Math.min(listView.count - 1, index))
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
        listView.positionViewAtIndex(next, ListView.Contain)
    }

    Timer {
        id: typeAheadTimer
        interval: 1000
        repeat: false
        onTriggered: root.typeAheadBuffer = ""
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
        if (!path || !viewModel)
            return false

        var idx = rowForPath(path)
        if (idx < 0) {
            pendingFocusPath = path
            pendingFocusReveal = (reveal !== false)
            return false
        }

        pendingFocusPath = ""
        pendingFocusReveal = true
        listView.forceActiveFocus()
        selectIndex(idx, false, false)
        if (reveal !== false)
            listView.positionViewAtIndex(idx, ListView.Contain)
        return true
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
    readonly property int colName: root.width - colSize - colModified - colType
    readonly property int colSize: 110
    readonly property int colModified: 140
    readonly property int colType: 80

    Column {
        anchors.fill: parent

        // Header row
        Rectangle {
            width: root.width
            height: root.rowHeight
            color: Theme.mantle
            radius: Theme.radiusMedium

            // Cover the bottom corners so only the top is rounded
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: parent.radius
                color: parent.color
            }

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
                        { key: "type",     label: "Type",        width: root.colType }
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
                            onPressed: {
                                wheelScroller.stopAndSettle()
                                root.interactionStarted()
                            }
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
            height: root.height - root.rowHeight
            clip: true

            onCountChanged: Qt.callLater(root.refreshFolderItemCounts)

            focus: visible
            keyNavigationEnabled: false
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
                if (count <= 0)
                    return
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
            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Home) {
                    root.moveSelectionTo(0, event.modifiers & Qt.ShiftModifier)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_End) {
                    root.moveSelectionTo(count - 1, event.modifiers & Qt.ShiftModifier)
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    root.activateCurrentSelection()
                    event.accepted = true
                    return
                }
                if (event.key === Qt.Key_Escape) {
                    if (root.typeAheadBuffer.length > 0) {
                        root.typeAheadBuffer = ""
                        typeAheadTimer.stop()
                    } else if (root.selectedIndices.length > 0) {
                        root.clearSelection()
                    }
                    event.accepted = true
                    return
                }
                root.handleTypeAhead(event)
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            model: root.viewModel

            delegate: Item {
                id: detRow
                width: listView.width
                height: root.rowHeight
                Accessible.role: Accessible.ListItem
                Accessible.name: fileName + (isDir ? ", folder" : ", " + fileType + ", " + fileSizeText)
                Accessible.selected: isSelected

                required property int index
                required property string fileName
                required property string filePath
                required property string fileSizeText
                required property string fileModifiedText
                required property string fileType
                required property bool isDir
                required property string fileIconName
                required property string gitStatus
                required property string gitStatusIcon

                readonly property bool isSelected: root.selectedIndices.indexOf(index) >= 0

                property bool dragStarted: false

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

                            // Icon with git badge
                            Item {
                                width: root.detailIconSize
                                height: root.detailIconSize
                                anchors.verticalCenter: parent.verticalCenter

                                readonly property bool isImage: !detRow.isDir &&
                                    /\.(png|jpg|jpeg|gif|webp|bmp)$/i.test(detRow.filePath)
                                readonly property bool isVideo: !detRow.isDir &&
                                    /\.(mp4|mkv|avi|mov|wmv|flv|webm|m4v|mpg|mpeg|3gp|ts)$/i.test(detRow.filePath)
                                readonly property bool hasThumbnail: !fileOps.isRemotePath(detRow.filePath) && (isImage || isVideo)

                                Image {
                                    anchors.fill: parent
                                    visible: !parent.hasThumbnail
                                    source: "image://icon/" + detRow.fileIconName
                                    sourceSize: Qt.size(root.detailIconSize, root.detailIconSize)
                                    asynchronous: false
                                }

                                Image {
                                    anchors.fill: parent
                                    visible: parent.hasThumbnail
                                    fillMode: Image.PreserveAspectFit
                                    source: parent.hasThumbnail ? ("image://thumbnail/" + detRow.filePath) : ""
                                    sourceSize: Qt.size(64 * Screen.devicePixelRatio, 64 * Screen.devicePixelRatio)
                                    asynchronous: true
                                }

                                Loader {
                                    active: detRow.gitStatus !== ""
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: -3
                                    anchors.bottomMargin: -3
                                    width: 9
                                    height: 9
                                    sourceComponent: {
                                        switch (detRow.gitStatusIcon) {
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
                            text: {
                                if (detRow.isDir) {
                                    var cnt = root.folderItemCounts[detRow.filePath]
                                    if (cnt !== undefined)
                                        return cnt + (cnt === 1 ? " item" : " items")
                                    return "—"
                                }
                                return detRow.fileSizeText
                            }
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
                                if (!detRow.isSelected)
                                    root.selectIndex(detRow.index, false, false)
                                var paths = root.selectedIndices.length > 1
                                    ? root.selectedIndices.map(function(i) { return root.pathForRow(i) })
                                    : [detRow.filePath]
                                detRow.dragStarted = true
                                dragHelper.startDrag(paths, detRow.fileIconName, paths.length)
                            }
                        }

                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                var mapped = rowMa.mapToItem(null, mouse.x, mouse.y)
                                if (detRow.isSelected) {
                                    root.contextMenuRequested(
                                        detRow.filePath,
                                        detRow.isDir,
                                        Qt.point(mapped.x, mapped.y)
                                    )
                                } else {
                                    root.contextMenuRequested("", false, Qt.point(mapped.x, mapped.y))
                                }
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
                        paths.push(s.startsWith("file://") ? decodeURIComponent(s.substring(7)) : s)
                    }
                    if (paths.length === 0) return
                    // Don't move files into the directory they're already in
                    var allSameDir = paths.every(function(p) {
                        var parentDir = p.substring(0, p.lastIndexOf("/"))
                        return parentDir === root.currentPath
                    })
                    if (allSameDir) return
                    if (drop.proposedAction === Qt.MoveAction)
                        root.transferRequested(paths, root.currentPath, true)
                    else
                        root.transferRequested(paths, root.currentPath, false)
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

                onWheel: (wheel) => {
                    if (wheel.modifiers & Qt.ControlModifier) {
                        wheelScroller.stopAndSettle()
                        root.interactionStarted()
                        var delta = wheelScroller.deltaFor(wheel)
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
                    var idx = listView.indexAt(mouse.x + listView.contentX, mouse.y + listView.contentY)
                    if (idx >= 0) {
                        mouse.accepted = false
                        return
                    }
                    wheelScroller.stopAndSettle()
                    root.interactionStarted()
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

    KineticWheelScroller {
        id: wheelScroller
        anchors.fill: parent
        z: 12
        flickable: listView
        wheelStep: 42
        mouseWheelMultiplier: 0.75
        minVelocity: 135
        maxVelocity: 3900
        kineticGain: 1.01
        onScrollStarted: root.interactionStarted()
    }

    Connections {
        target: root.viewModel
        ignoreUnknownSignals: true

        function onModelReset() {
            root.schedulePendingFocus()
        }

        function onRowsInserted() {
            root.schedulePendingFocus()
        }
    }

    Component { id: gitModifiedIcon;   IconGitModified   { size: 9 } }
    Component { id: gitStagedIcon;     IconGitStaged     { size: 9 } }
    Component { id: gitUntrackedIcon;  IconGitUntracked  { size: 9 } }
    Component { id: gitDeletedIcon;    IconGitDeleted    { size: 9 } }
    Component { id: gitRenamedIcon;    IconGitRenamed    { size: 9 } }
    Component { id: gitConflictedIcon; IconGitConflicted { size: 9 } }
    Component { id: gitIgnoredIcon;    IconGitIgnored    { size: 9 } }
    Component { id: gitDirtyIcon;      IconGitDirty      { size: 9 } }
}
