import QtQuick
import QtQuick.Controls
import HyprFM
import Quill as Q

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
        // Reset any sticky `interactive=false` left behind by an in-flight
        // rubberband / drag in the previous directory — otherwise the wheel
        // handler short-circuits until the user clicks something.
        if (listView) listView.interactive = true
        Qt.callLater(refreshFolderItemCounts)
    }
    onVisibleChanged: {
        if (visible)
            Qt.callLater(refreshFolderItemCounts)
        else
            folderItemCounts = ({})
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
        if (!root.visible || !viewModel || listView.count <= 0 || !viewModel.folderItemCounts)
            return

        var first = Math.max(0, Math.floor(listView.contentY / root.rowHeight) - 12)
        var last = Math.min(listView.count - 1,
            Math.ceil((listView.contentY + listView.height) / root.rowHeight) + 12)

        var paths = []
        for (var i = first; i <= last; ++i) {
            if (isDirForRow(i)) {
                var p = pathForRow(i)
                if (p && !fileOps.isRemotePath(p) && !fileOps.isSlowPath(p))
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

    // Selection/cursor are model row indices. Reconcile them when the model
    // inserts/removes rows so files moved out of this directory stop dragging
    // a stale highlight (and surviving rows keep the right one).
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

    // Columns. Keys and defaults live in ConfigManager::knownListColumns();
    // this table only adds what the view needs to draw them.
    readonly property var columnCatalog: ({
        name:        { label: "Name",        role: "fileName",         align: Text.AlignLeft,  sortable: true },
        size:        { label: "Size",        role: "fileSizeText",     align: Text.AlignRight, sortable: true },
        modified:    { label: "Modified",    role: "fileModifiedText", align: Text.AlignRight, sortable: true },
        type:        { label: "Type",        role: "fileType",         align: Text.AlignLeft,  sortable: true },
        permissions: { label: "Permissions", role: "filePermissions",  align: Text.AlignLeft,  sortable: false },
        owner:       { label: "Owner",       role: "fileOwner",        align: Text.AlignLeft,  sortable: false },
        group:       { label: "Group",       role: "fileGroup",        align: Text.AlignLeft,  sortable: false },
        created:     { label: "Created",     role: "fileCreatedText",  align: Text.AlignRight, sortable: false },
        accessed:    { label: "Accessed",    role: "fileAccessedText", align: Text.AlignRight, sortable: false },
        extension:   { label: "Ext",         role: "fileExtension",    align: Text.AlignLeft,  sortable: false },
        mime:        { label: "MIME type",   role: "mimeType",         align: Text.AlignLeft,  sortable: false },
        git:         { label: "Git",         role: "gitStatus",        align: Text.AlignLeft,  sortable: false },
        symlink:     { label: "Link target", role: "symlinkTarget",    align: Text.AlignLeft,  sortable: false }
    })
    // Display order and widths, mirrored from config so both panes stay in sync
    // and drags can update them live before saving on release.
    property var columns: config.listColumns
    property var columnWidths: config.listColumnWidths
    readonly property int minColumnWidth: 40
    readonly property int fixedColumnsWidth: {
        var total = 0
        for (var i = 0; i < columns.length; ++i)
            if (columns[i] !== "name")
                total += columnWidth(columns[i])
        return total
    }
    readonly property int colName: Math.max(120, root.width - 16 - fixedColumnsWidth)
    // Header drag state (index of the column being moved, -1 when idle).
    property int dragSourceIndex: -1
    // Only reorders and toggles animate; resizes and the initial layout snap,
    // otherwise cells lag behind the grip while dragging.
    readonly property bool animateColumns: dragSourceIndex >= 0 || columnsAnimTimer.running
    Timer { id: columnsAnimTimer; interval: Theme.animDuration + 50 }

    // Header and row cells iterate this model instead of the array so a
    // reorder is a ListModel.move(): delegates survive and the Row's move
    // transition animates them into place.
    ListModel { id: columnModel }

    onColumnsChanged: syncColumnModel()
    Component.onCompleted: syncColumnModel()

    function syncColumnModel() {
        var current = []
        for (var i = 0; i < columnModel.count; ++i)
            current.push(columnModel.get(i).key)
        // remove what is gone
        for (i = current.length - 1; i >= 0; --i) {
            if (columns.indexOf(current[i]) < 0) {
                columnModel.remove(i)
                current.splice(i, 1)
            }
        }
        // insert what is new, then fix the order with moves
        for (i = 0; i < columns.length; ++i) {
            var at = current.indexOf(columns[i])
            if (at < 0) {
                columnModel.insert(i, { key: columns[i] })
                current.splice(i, 0, columns[i])
            } else if (at !== i) {
                columnModel.move(at, i, 1)
                current.splice(i, 0, current.splice(at, 1)[0])
            }
        }
    }

    function columnWidth(key) {
        if (key === "name")
            return colName
        var w = columnWidths[key]
        return w === undefined ? 80 : w
    }
    function setColumnWidth(key, width) {
        var w = Object.assign({}, columnWidths)
        w[key] = Math.max(minColumnWidth, Math.round(width))
        columnWidths = w
    }
    function moveColumn(from, to) {
        if (from < 0 || to < 0 || from === to || to >= columns.length) return
        var arr = columns.slice()
        var key = arr.splice(from, 1)[0]
        arr.splice(to, 0, key)
        columns = arr
    }
    function toggleColumn(key, on) {
        var arr = columns.filter(function(k) { return k !== key })
        if (on) arr.push(key)
        columnsAnimTimer.restart()
        columns = arr
        saveColumns()
    }
    function saveColumns() {
        config.saveListColumns(columns, columnWidths)
    }
    // Header x → index of the column slot under it (for reorder).
    function columnIndexAt(x) {
        var acc = 0
        for (var i = 0; i < columns.length; ++i) {
            var w = columnWidth(columns[i])
            if (x < acc + w) return i
            acc += w
        }
        return columns.length - 1
    }
    function cellText(key, row) {
        if (key === "size" && row.isDir) {
            var cnt = root.folderItemCounts[row.filePath]
            if (cnt !== undefined)
                return cnt + (cnt === 1 ? " item" : " items")
            return "\u2014"
        }
        var value = row.model[columnCatalog[key].role]
        return value === undefined || value === null ? "" : String(value)
    }

    Connections {
        target: config
        function onListColumnsChanged() {
            root.columns = config.listColumns
            root.columnWidths = config.listColumnWidths
        }
    }

    // Right-click on the header: pick which columns to show. Same look and
    // entrance as ContextMenu, one row per optional column.
    Item {
        id: columnMenu
        anchors.fill: parent
        visible: false
        z: 50

        function openAt(px, py) {
            menuBox.x = Math.max(0, Math.min(px, root.width - menuBox.width))
            menuBox.y = Math.max(0, Math.min(py, root.height - menuBox.height))
            menuBox.opacity = 0
            menuBox.scale = 0.88
            visible = true
            menuOpenAnim.restart()
        }
        function close() { menuCloseAnim.restart() }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            onPressed: columnMenu.close()
        }

        ParallelAnimation {
            id: menuOpenAnim
            NumberAnimation {
                target: menuBox; property: "opacity"; from: 0; to: 1; duration: Theme.animDurationFast
                easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
            }
            NumberAnimation {
                target: menuBox; property: "scale"; from: 0.88; to: 1; duration: Theme.animDurationSlow
                easing.type: Easing.OutBack; easing.overshoot: 0.8
            }
            NumberAnimation {
                target: menuBox; property: "yOffset"; from: -8; to: 0; duration: Theme.animDuration
                easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
            }
        }
        SequentialAnimation {
            id: menuCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: menuBox; property: "opacity"; to: 0; duration: Theme.animDurationFast
                    easing.type: Theme.animEasingExit; easing.bezierCurve: Theme.animBezierCurve
                }
                NumberAnimation {
                    target: menuBox; property: "scale"; to: 0.92; duration: Theme.animDurationFast
                    easing.type: Theme.animEasingExit; easing.bezierCurve: Theme.animBezierCurve
                }
                NumberAnimation {
                    target: menuBox; property: "yOffset"; to: -4; duration: Theme.animDurationFast
                    easing.type: Theme.animEasingExit; easing.bezierCurve: Theme.animBezierCurve
                }
            }
            ScriptAction { script: columnMenu.visible = false }
        }

        Item {
            id: menuBox
            width: menuColumn.width + 12
            height: menuColumn.height + 12
            opacity: 0
            scale: 0.88
            transformOrigin: Item.TopLeft
            property real yOffset: 0
            transform: Translate { y: menuBox.yOffset }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusLarge
                color: Theme.crust
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                border.width: 1
            }

            Column {
                id: menuColumn
                anchors.centerIn: parent
                width: 200
                spacing: 2

                Repeater {
                    model: Object.keys(root.columnCatalog).filter(function(k) { return k !== "name" })
                    delegate: Rectangle {
                        required property string modelData
                        readonly property bool shown: root.columns.indexOf(modelData) >= 0
                        objectName: "columnMenuItem_" + modelData
                        width: menuColumn.width
                        height: 32
                        radius: Theme.radiusMedium
                        color: itemMa.containsMouse
                            ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                            : "transparent"
                        Behavior on color {
                            ColorAnimation { duration: 100; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve }
                        }
                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 8
                            IconCheck {
                                anchors.verticalCenter: parent.verticalCenter
                                size: 16
                                color: Theme.accent
                                opacity: shown ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.animDurationFast } }
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.columnCatalog[modelData].label
                                font.pointSize: Theme.fontNormal
                                color: Theme.text
                            }
                        }
                        MouseArea {
                            id: itemMa
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.toggleColumn(modelData, !shown)
                                columnMenu.close()
                            }
                        }
                    }
                }
            }
        }
    }

    Item {
        anchors.fill: parent

        // Wheel scroller sits between the list and the header. As a MouseArea
        // it claims the arrow cursor for everything under it, so the header
        // (pointer + resize cursors) must be a sibling stacked above it.
        KineticWheelScroller {
            id: wheelScroller
            anchors.fill: parent
            z: 12
            flickable: listView
            wheelStep: 42
            mouseWheelMultiplier: 0.75
            touchpadMultiplier: 1.35
            minVelocity: 135
            maxVelocity: 3900
            kineticGain: 1.01
            onScrollStarted: root.interactionStarted()
        }

        // Header row
        Rectangle {
            z: 13
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
                move: Transition {
                    enabled: root.animateColumns
                    NumberAnimation { properties: "x"; duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve }
                }
                add: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.animDuration }
                }

                Repeater {
                    model: columnModel

                    delegate: Item {
                        id: hdrItem
                        required property int index
                        required property string key
                        readonly property var spec: root.columnCatalog[key]
                        readonly property bool fills: key === "name"   // takes the remaining width, no grip
                        objectName: "headerColumn_" + key
                        width: root.columnWidth(key)
                        height: 28
                        // Earlier columns stack above later ones so a grip can
                        // straddle the boundary without the neighbour eating it.
                        z: 100 - index

                        Rectangle {
                            anchors.fill: parent
                            color: root.dragSourceIndex === hdrItem.index
                                ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                                : hdrMa.containsMouse
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
                                text: hdrItem.spec.label
                                color: root.sortColumn === hdrItem.key ? Theme.accent : Theme.subtext
                                font.pointSize: Theme.fontSmall
                                font.bold: root.sortColumn === hdrItem.key
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            IconChevronDown {
                                visible: hdrItem.spec.sortable && root.sortColumn === hdrItem.key
                                size: 12
                                color: Theme.accent
                                rotation: root.sortAscending ? 180 : 0
                                Behavior on rotation {
                                    NumberAnimation { duration: 200; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve }
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
                            objectName: "headerArea_" + hdrItem.key
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            cursorShape: Qt.PointingHandCursor
                            property real pressX: 0
                            property bool dragging: false
                            onPressed: (mouse) => {
                                wheelScroller.stopAndSettle()
                                root.interactionStarted()
                                listView.forceActiveFocus()
                                if (mouse.button === Qt.RightButton) {
                                    var pt = mapToItem(root, mouse.x, mouse.y)
                                    columnMenu.openAt(pt.x, pt.y)
                                    return
                                }
                                pressX = mouse.x
                                dragging = false
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed || mouse.buttons !== Qt.LeftButton) return
                                if (!dragging && Math.abs(mouse.x - pressX) < 4) return
                                dragging = true
                                root.dragSourceIndex = hdrItem.index
                                // Live reorder: pointer position in header coordinates picks
                                // the slot; the model move keeps this delegate alive.
                                var headerX = mapToItem(hdrItem.parent, mouse.x, 0).x
                                var target = Math.max(0, root.columnIndexAt(headerX))
                                if (target !== hdrItem.index)
                                    root.moveColumn(hdrItem.index, target)
                            }
                            onReleased: (mouse) => {
                                if (mouse.button !== Qt.LeftButton) return
                                var wasDragging = dragging
                                dragging = false
                                root.dragSourceIndex = -1
                                if (wasDragging)
                                    root.saveColumns()
                                else if (hdrItem.spec.sortable && containsMouse)
                                    root.clickHeader(hdrItem.key)
                            }
                            onCanceled: { root.dragSourceIndex = -1; dragging = false }
                        }

                        // Resize grip straddling this column's right boundary.
                        // Name soaks up whatever width the others leave, so the
                        // line under the cursor only follows the cursor if we
                        // resize the neighbour on the side *away* from Name:
                        // Name at/left of the line → shrink the right column by
                        // dx; Name to the right → grow the left column by dx.
                        MouseArea {
                            objectName: "headerGrip_" + hdrItem.key
                            readonly property bool fillOnLeft: root.columns.indexOf("name") <= hdrItem.index
                            readonly property string targetKey: fillOnLeft
                                ? (hdrItem.index + 1 < columnModel.count ? columnModel.get(hdrItem.index + 1).key : "")
                                : hdrItem.key
                            readonly property int direction: fillOnLeft ? -1 : 1
                            visible: targetKey !== ""
                            anchors.right: parent.right
                            anchors.rightMargin: -6
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 12
                            hoverEnabled: true
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            property real startX: 0
                            property int startWidth: 0

                            // Boundary highlight so it is obvious what you are grabbing
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.topMargin: 2
                                anchors.bottomMargin: 2
                                width: 2
                                radius: 1
                                color: Theme.accent
                                opacity: parent.pressed ? 1 : (parent.containsMouse ? 0.7 : 0)
                                Behavior on opacity { NumberAnimation { duration: Theme.animDurationFast } }
                            }
                            onPressed: (mouse) => {
                                root.interactionStarted()
                                startX = mapToItem(root, mouse.x, 0).x
                                startWidth = root.columnWidth(targetKey)
                            }
                            onPositionChanged: (mouse) => {
                                if (!pressed) return
                                var dx = mapToItem(root, mouse.x, 0).x - startX
                                root.setColumnWidth(targetKey, startWidth + direction * dx)
                            }
                            onReleased: root.saveColumns()
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
            objectName: "listView"
            y: root.rowHeight
            width: root.width
            height: root.height - root.rowHeight
            clip: true
            reuseItems: true
            cacheBuffer: 512

            onCountChanged: Qt.callLater(root.refreshFolderItemCounts)
            onMovementEnded: Qt.callLater(root.refreshFolderItemCounts)

            focus: visible
            keyNavigationEnabled: false
            boundsMovement: Flickable.StopAtBounds
            boundsBehavior: Flickable.StopAtBounds
            rebound: Transition {
                NumberAnimation {
                    properties: "x,y"
                    duration: Theme.animDurationSlow + 60
                    easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
                }
            }
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
                if (config.keyEventMatches("open", event.key, event.modifiers)) {
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

                // The remove transition leaves opacity/scale animated down, and
                // reuseItems recycles that item as-is (view transitions never
                // re-run on reuse) — without this reset the row comes back blank.
                ListView.onReused: { opacity = 1; scale = 1 }
                Accessible.role: Accessible.ListItem
                Accessible.name: fileName + (isDir ? ", folder" : ", " + fileType + ", " + fileSizeText)
                Accessible.selected: isSelected

                required property int index
                required property var model
                required property string fileName
                required property string filePath
                required property var fileModified
                required property string fileSizeText
                required property string fileModifiedText
                required property string fileType
                required property bool isDir
                required property string fileIconName
                required property string gitStatus
                required property string gitStatusIcon
                required property bool hasImagePreview
                required property bool hasVideoPreview
                required property bool hasPdfPreview

                readonly property bool isSelected: root.selectedIndices.indexOf(index) >= 0
                readonly property bool isCutPending: clipboard.isCut && clipboard.contains(detRow.filePath)
                readonly property bool isPastePending: fileOps.pendingTargetPaths.indexOf(detRow.filePath) >= 0

                property bool dragStarted: false

                            // Row cells, one Loader per column. `key` is the Loader's required
                            // property (visible to the loaded item as its context object);
                            // detRow is in scope because the components live in the delegate.
                Component {
                    id: nameCellComponent
                    Row {
                        anchors.fill: parent
                        spacing: 6

                        // Icon with git badge
                        Item {
                            width: root.detailIconSize
                            height: root.detailIconSize
                            anchors.verticalCenter: parent.verticalCenter

                            readonly property bool hasThumbnail: !fileOps.isRemotePath(detRow.filePath)
                                && (detRow.hasImagePreview || detRow.hasVideoPreview
                                    || detRow.hasPdfPreview)

                            Image {
                                anchors.fill: parent
                                visible: !parent.hasThumbnail
                                source: "image://icon/" + detRow.fileIconName + "?theme=" + config.iconTheme
                                sourceSize: Qt.size(root.detailIconSize * Screen.devicePixelRatio,
                                                    root.detailIconSize * Screen.devicePixelRatio)
                                asynchronous: true
                            }

                            Image {
                                anchors.fill: parent
                                visible: parent.hasThumbnail
                                fillMode: Image.PreserveAspectFit
                                source: !parent.hasThumbnail
                                    ? ""
                                    : detRow.hasPdfPreview
                                        ? ("image://pdfpreview/" + encodeURIComponent(detRow.filePath)
                                           + "?page=0")
                                        : ("image://thumbnail/" + detRow.filePath
                                           + "?mtime=" + new Date(detRow.fileModified).getTime())
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
                                opacity: detRow.isCutPending ? 1 : 0
                                scale: detRow.isCutPending ? 1 : 0.88
                                visible: opacity > 0

                                Behavior on opacity { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve } }
                                Behavior on scale { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve } }

                                IconScissors {
                                    anchors.centerIn: parent
                                    size: 7
                                    color: Theme.warning
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
                                opacity: detRow.isPastePending ? 1 : 0
                                scale: detRow.isPastePending ? 1 : 0.9
                                visible: opacity > 0

                                Behavior on opacity { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve } }
                                Behavior on scale { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve } }

                                Q.Spinner {
                                    anchors.centerIn: parent
                                    size: "small"
                                    color: Theme.accent
                                    running: detRow.isPastePending
                                    scale: 0.6
                                }
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
                            textFormat: Text.PlainText
                            text: detRow.fileName
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }

                Component {
                    id: textCellComponent
                    Text {
                        anchors.fill: parent
                        verticalAlignment: Text.AlignVCenter
                        textFormat: Text.PlainText
                        text: root.cellText(key, detRow)
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                        elide: Text.ElideRight
                        horizontalAlignment: root.columnCatalog[key].align
                        rightPadding: 8
                        leftPadding: 4
                    }
                }



                DropArea {
                    id: folderDropArea
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    enabled: detRow.isDir && !detRow.isSelected

                    onDropped: (drop) => {
                        var paths = root.dropPaths(drop)
                        if (paths.length === 0) return
                        var dominated = paths.some(function(p) {
                            return detRow.filePath === p || detRow.filePath.startsWith(p + "/")
                        })
                        if (dominated) return
                        root.transferRequested(paths, detRow.filePath, drop.proposedAction !== Qt.CopyAction)
                        drop.acceptProposedAction()
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSmall
                    opacity: detRow.dragStarted ? 0.5 : 1.0
                    color: {
                        if (folderDropArea.containsDrag)
                            return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.3)
                        if (detRow.isSelected)
                            return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                        if (rowMa.containsMouse)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                        // Alternating rows
                        if (detRow.index % 2 === 0)
                            return "transparent"
                        return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.025)
                    }
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }
                    border.color: folderDropArea.containsDrag ? Theme.accent : (detRow.isSelected ? Theme.accent : "transparent")
                    border.width: folderDropArea.containsDrag ? 2 : (detRow.isSelected ? 1 : 0)

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 0
                        move: Transition {
                            enabled: root.animateColumns
                            NumberAnimation { properties: "x"; duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve }
                        }

                        // Every column after Name. Values are read loosely from
                        // the row's model object because the search results
                        // model lacks the optional roles.
                        Repeater {
                            model: columnModel
                            delegate: Loader {
                                required property string key
                                width: root.columnWidth(key)
                                height: parent ? parent.height : 0
                                sourceComponent: key === "name" ? nameCellComponent : textCellComponent
                            }
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
                            // Claim focus immediately so arrow keys / type-ahead
                            // work after clicking a row. Without this, focus can
                            // linger on the toolbar / path bar / other pane and
                            // ListView.Keys handlers never fire.
                            listView.forceActiveFocus()
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
                                // See FileGridView: an unselected row is
                                // selected first so one right-click reaches the
                                // item menu; an existing multi-selection stays.
                                if (!detRow.isSelected)
                                    root.selectIndex(detRow.index, false, false)
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
                    var paths = root.dropPaths(drop)
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

                // Double-click on empty space goes up a directory (issue #14).
                onDoubleClicked: (mouse) => {
                    if (mouse.button !== Qt.LeftButton) return
                    var parent = root.currentPath ? fileOps.parentPath(root.currentPath) : ""
                    if (parent && parent !== root.currentPath)
                        root.fileActivated(parent, true)
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


    Connections {
        target: root.viewModel
        ignoreUnknownSignals: true

        function onModelReset() {
            // Indices no longer map to the same files; drop the stale selection.
            root.clearSelection()
            root.schedulePendingFocus()
        }

        function onRowsInserted(_p, first, last) {
            root.reconcileAfterInsert(first, last)
            root.schedulePendingFocus()
        }

        function onRowsRemoved(_p, first, last) {
            root.reconcileAfterRemove(first, last)
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
