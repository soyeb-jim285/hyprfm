import QtQuick
import QtQuick.Controls
import HyprFM

FocusScope {
    id: root
    Accessible.role: Accessible.Pane
    Accessible.name: "Miller columns view"

    property var fileModel: null
    property string currentPath: ""

    property var selectedIndices: currentColumn.selectedIndices
    property int lastSelectedIndex: currentColumn.lastSelectedIndex
    property int cursorIndex: currentColumn.cursorIndex

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)
    signal selectionChanged()
    signal interactionStarted()
    signal transferRequested(var paths, string destinationPath, bool moveOperation)

    // Track which folder in parent column leads to currentPath
    readonly property string parentPath: {
        if (!currentPath || currentPath === "/") return ""
        var parent = fileOps.parentPath(currentPath)
        return parent === currentPath ? "" : parent
    }

    readonly property string currentDirName: currentPath ? fileOps.displayNameForPath(currentPath) : ""

    property int rowHeight: 28
    readonly property int minRowHeight: 22
    readonly property int maxRowHeight: 56
    readonly property int millerIconSize: Math.round(rowHeight * 0.571)  // 16 at default 28

    onCurrentPathChanged: {
        if (parentPath) {
            millerParentModel.setRootPath(parentPath)
        } else {
            millerParentModel.setRootPath("")
        }
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
        var idx = currentColumn.cursorIndex >= 0 ? currentColumn.cursorIndex
            : (currentColumn.selectedIndices.length > 0 ? currentColumn.selectedIndices[currentColumn.selectedIndices.length - 1] : -1)
        if (idx < 0 || !fileModel) {
            millerPreviewModel.setRootPath("")
            previewColumn.previewFilePath = ""
            previewColumn.previewIsDir = false
            return
        }
        var fp = currentColumn.pathForRow(idx)
        var isDir = currentColumn.isDirForRow(idx)
        previewColumn.previewFilePath = fp
        previewColumn.previewIsDir = isDir
        if (isDir) {
            millerPreviewModel.setRootPath(fp)
        } else {
            millerPreviewModel.setRootPath("")
        }
    }

    Row {
        anchors.fill: parent

        // ── Parent column (20%) ───────────────────────────────────────────
        ListView {
            id: parentColumn
            width: Math.floor(root.width * 0.2)
            height: root.height
            clip: true
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
                            source: "image://icon/" + parentDelegate.fileIconName
                            sourceSize: Qt.size(root.millerIconSize, root.millerIconSize)
                            asynchronous: false
                            opacity: parentDelegate.isCurrentDir ? 0.95 : 0.8
                        }

                        Text {
                            width: parent.width - root.millerIconSize - parent.spacing - (parentDelegate.isDir ? root.millerIconSize : 0) - parent.anchors.leftMargin - parent.anchors.rightMargin
                            anchors.verticalCenter: parent.verticalCenter
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

        Rectangle {
            width: 1
            height: root.height
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
        }

        // ── Current column (50%) ─────────────────────────────────────────
        ListView {
            id: currentColumn
            width: Math.floor(root.width * 0.5) - 1
            height: root.height
            clip: true
            model: root.fileModel
            focus: root.visible
            boundsBehavior: Flickable.StopAtBounds
            boundsMovement: Flickable.StopAtBounds
            keyNavigationEnabled: false

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
                function onModelReset() { currentColumn.schedulePendingFocus() }
                function onRowsInserted() { currentColumn.schedulePendingFocus() }
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
                if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
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

                required property int index
                required property string fileName
                required property string filePath
                required property string fileSizeText
                required property string fileModifiedText
                required property bool isDir
                required property string fileIconName
                required property string gitStatus
                required property string gitStatusIcon

                readonly property bool isSelected: currentColumn.selectedIndices.indexOf(index) >= 0

                property bool dragStarted: false

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSmall
                    opacity: currentDelegate.dragStarted ? 0.5 : 1.0
                    color: {
                        if (currentDelegate.isSelected)
                            return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                        if (currentDelegateMa.containsMouse)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                        if (currentDelegate.index % 2 === 1)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.025)
                        return "transparent"
                    }
                    border.color: currentDelegate.isSelected ? Theme.accent : "transparent"
                    border.width: currentDelegate.isSelected ? 1 : 0

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        spacing: 6

                        Item {
                            width: root.millerIconSize + 2; height: root.millerIconSize + 2
                            anchors.verticalCenter: parent.verticalCenter

                            readonly property bool isImage: !currentDelegate.isDir &&
                                /\.(png|jpg|jpeg|gif|webp|bmp)$/i.test(currentDelegate.filePath)
                            readonly property bool isVideo: !currentDelegate.isDir &&
                                /\.(mp4|mkv|avi|mov|wmv|flv|webm|m4v|mpg|mpeg|3gp|ts)$/i.test(currentDelegate.filePath)
                            readonly property bool hasThumbnail: !fileOps.isRemotePath(currentDelegate.filePath) && (isImage || isVideo)

                            Image {
                                anchors.fill: parent
                                visible: !parent.hasThumbnail
                                source: "image://icon/" + currentDelegate.fileIconName
                                sourceSize: Qt.size(root.millerIconSize + 2, root.millerIconSize + 2)
                                asynchronous: false
                            }

                            Image {
                                anchors.fill: parent
                                visible: parent.hasThumbnail
                                fillMode: Image.PreserveAspectFit
                                source: parent.hasThumbnail ? ("image://thumbnail/" + currentDelegate.filePath) : ""
                                sourceSize: Qt.size(64 * Screen.devicePixelRatio, 64 * Screen.devicePixelRatio)
                                asynchronous: true
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
                            text: currentDelegate.fileName
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
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
                            if (currentDelegate.isSelected) {
                                root.contextMenuRequested(
                                    currentDelegate.filePath,
                                    currentDelegate.isDir,
                                    Qt.point(mapped.x, mapped.y)
                                )
                            } else {
                                root.contextMenuRequested("", false, Qt.point(mapped.x, mapped.y))
                            }
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
                    var urls = drop.urls
                    var paths = []
                    for (var i = 0; i < urls.length; i++) {
                        var s = urls[i].toString()
                        paths.push(s.startsWith("file://") ? decodeURIComponent(s.substring(7)) : s)
                    }
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
                minVelocity: 135
                maxVelocity: 3900
                kineticGain: 1.01
                onScrollStarted: root.interactionStarted()
            }
        }

        Rectangle {
            width: 1
            height: root.height
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
        }

        // ── Preview column (30%) ─────────────────────────────────────────
        Item {
            id: previewColumn
            width: root.width - Math.floor(root.width * 0.2) - Math.floor(root.width * 0.5) - 1
            height: root.height
            clip: true

            property string previewFilePath: ""
            property bool previewIsDir: false

            // Rich preview data (like QuickPreview)
            property var fileProps: ({})
            property var textPreview: ({ content: "", truncated: false, isBinary: false, error: "" })
            property var directoryPreview: ({ entries: [], truncated: false, error: "", count: 0 })
            property var pdfPreview: ({ localPath: "", pageCount: 0, error: "" })
            property var fileMetadata: ({})
            property string metadataHint: ""
            property int pdfPageIndex: 0
            property real pdfWheelAccumulator: 0

            readonly property string _mime: fileProps.mimeType || ""
            readonly property bool isRemoteUri: previewFilePath !== "" && fileOps.isRemotePath(previewFilePath)
            readonly property bool isTrashUri: previewFilePath.startsWith("trash:///")
            readonly property bool isArchive: !previewIsDir && fileOps.isArchive(previewFilePath)
            readonly property bool isImage: !isRemoteUri && !previewIsDir && _mime.startsWith("image/")
            readonly property bool isVideo: !isRemoteUri && !previewIsDir && _mime.startsWith("video/")
            readonly property bool isAudio: !isRemoteUri && !previewIsDir && (_mime.startsWith("audio/") || false)
            readonly property bool isPdf: !isRemoteUri && !previewIsDir && _mime === "application/pdf"
            readonly property bool pdfPreviewAvailable: previewService.pdfPreviewAvailable
            readonly property bool videoPreviewAvailable: runtimeFeatures.ffmpegAvailable
            readonly property bool textHighlightAvailable: runtimeFeatures.batAvailable
            readonly property bool hasVisualPreview: isImage || (isVideo && videoPreviewAvailable)
            readonly property string visualSource: {
                if (!hasVisualPreview || previewFilePath === "") return ""
                if (isVideo || isTrashUri) return "image://thumbnail/" + previewFilePath
                return "file://" + previewFilePath
            }
            readonly property string pdfImageSource: {
                if (!isPdf || !pdfPreview.localPath || pdfPreview.error !== "")
                    return ""
                return "image://pdfpreview/" + encodeURIComponent(pdfPreview.localPath)
                    + "?page=" + pdfPageIndex
            }
            readonly property string pdfPageLabel: {
                if (!isPdf || pdfPreview.pageCount <= 0)
                    return ""
                return "Page " + (pdfPageIndex + 1) + " of " + pdfPreview.pageCount
            }
            readonly property bool isText: {
                if (isRemoteUri || previewIsDir || isPdf || isImage || isVideo || isAudio || isArchive) return false
                if (_mime.startsWith("text/")) return true
                var textMimes = [
                    "application/json", "application/xml", "application/x-yaml",
                    "application/toml", "application/x-shellscript",
                    "application/javascript", "application/typescript",
                    "application/x-tex", "application/x-makefile",
                    "application/x-desktop", "application/x-ruby",
                    "application/x-perl", "application/x-python"
                ]
                if (textMimes.indexOf(_mime) >= 0) return true
                var ext = previewFileName.lastIndexOf(".") >= 0
                    ? previewFileName.substring(previewFileName.lastIndexOf(".") + 1).toLowerCase() : ""
                if (ext === "") return previewFilePath !== ""
                var textExt = ["txt", "md", "json", "yaml", "yml", "toml", "ini", "cfg", "conf",
                               "sh", "bash", "zsh", "fish", "py", "js", "ts", "tsx", "jsx",
                               "css", "html", "htm", "xml", "c", "cpp", "h", "hpp", "rs",
                               "go", "java", "tex", "rb", "lua", "vim", "log", "diff",
                               "patch", "cmake", "qml", "mk", "desktop"]
                return textExt.indexOf(ext) >= 0
            }

            readonly property string previewFileName: {
                if (fileProps.name)
                    return fileProps.name
                if (previewFilePath === "") return ""
                var idx = previewFilePath.lastIndexOf("/")
                return idx >= 0 ? previewFilePath.substring(idx + 1) : previewFilePath
            }

            readonly property var metadataEntries: {
                var result = []
                var md = previewColumn.fileMetadata || {}
                var keys = Object.keys(md)
                for (var i = 0; i < keys.length; ++i) {
                    var value = md[keys[i]]
                    if (value !== undefined && value !== null && String(value) !== "")
                        result.push({ label: keys[i], value: String(value) })
                }
                return result
            }

            readonly property string detailKind: {
                if (previewIsDir) return "Folder"
                if (isArchive) return "Archive"
                if (isAudio) return "Audio"
                if (isVideo) return "Video"
                if (fileProps.mimeDescription) return fileProps.mimeDescription
                var ext = previewFileName.lastIndexOf(".") >= 0
                    ? previewFileName.substring(previewFileName.lastIndexOf(".") + 1).toUpperCase() : ""
                return ext !== "" ? ext + " file" : "File"
            }

            function changePdfPage(delta) {
                if (!isPdf || pdfPreview.pageCount <= 0)
                    return
                pdfPageIndex = Math.max(0, Math.min(pdfPreview.pageCount - 1, pdfPageIndex + delta))
            }

            function handlePdfWheel(wheel) {
                if (!isPdf || pdfPreview.pageCount <= 1)
                    return

                var delta = 0
                if (wheel.angleDelta && wheel.angleDelta.y !== 0)
                    delta = wheel.angleDelta.y
                else if (wheel.pixelDelta && wheel.pixelDelta.y !== 0)
                    delta = wheel.pixelDelta.y * 3

                if (delta === 0)
                    return

                pdfWheelAccumulator += delta
                while (pdfWheelAccumulator >= 120) {
                    changePdfPage(-1)
                    pdfWheelAccumulator -= 120
                }
                while (pdfWheelAccumulator <= -120) {
                    changePdfPage(1)
                    pdfWheelAccumulator += 120
                }

                wheel.accepted = true
            }

            function refreshPreview() {
                if (previewFilePath === "") {
                    fileProps = ({})
                    textPreview = ({ content: "", truncated: false, isBinary: false, error: "" })
                    directoryPreview = ({ entries: [], truncated: false, error: "", count: 0 })
                    pdfPreview = ({ localPath: "", pageCount: 0, error: "" })
                    fileMetadata = ({})
                    metadataHint = ""
                    return
                }

                if (root.fileModel && root.fileModel.fileProperties)
                    fileProps = root.fileModel.fileProperties(previewFilePath)
                else
                    fileProps = ({})

                if (isText)
                    textPreview = previewService.loadTextPreview(previewFilePath)
                else
                    textPreview = ({ content: "", truncated: false, isBinary: false, error: "" })

                if (isPdf) {
                    pdfPreview = previewService.loadPdfPreview(previewFilePath)
                    if (pdfPageIndex >= (pdfPreview.pageCount || 0))
                        pdfPageIndex = 0
                } else {
                    pdfPreview = ({ localPath: "", pageCount: 0, error: "" })
                }

                if (previewIsDir)
                    directoryPreview = previewService.loadDirectoryPreview(previewFilePath)
                else if (isArchive)
                    directoryPreview = previewService.loadArchivePreview(previewFilePath)
                else
                    directoryPreview = ({ entries: [], truncated: false, error: "", count: 0 })

                fileMetadata = metadataExtractor.extract(previewFilePath)
                metadataHint = metadataExtractor.missingDepsHint(fileProps.mimeType || "")
            }

            onPreviewFilePathChanged: {
                pdfPageIndex = 0
                pdfWheelAccumulator = 0
                refreshPreview()
            }

            // ── Preview content area (top) + info bar (bottom) ───────────
            Column {
                anchors.fill: parent

                // Preview area
                Item {
                    id: previewArea
                    width: parent.width
                    height: parent.height - infoBar.height

                    // Directory listing
                    ListView {
                        id: previewDirList
                        anchors.fill: parent
                        visible: previewColumn.previewIsDir
                        model: millerPreviewModel
                        clip: true
                        interactive: true
                        boundsBehavior: Flickable.StopAtBounds

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Item {
                            width: previewDirList.width
                            height: 24

                            required property int index
                            required property string fileName
                            required property bool isDir
                            required property string fileIconName

                            Row {
                                anchors.fill: parent
                                anchors.leftMargin: 6
                                anchors.rightMargin: 4
                                spacing: 6

                                Image {
                                    width: 14; height: 14
                                    anchors.verticalCenter: parent.verticalCenter
                                    source: "image://icon/" + fileIconName
                                    sourceSize: Qt.size(14, 14)
                                    asynchronous: false
                                }

                                Text {
                                    width: parent.width - 14 - parent.spacing - parent.anchors.leftMargin - parent.anchors.rightMargin
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: fileName
                                    color: Theme.subtext
                                    font.pointSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    // Archive contents preview
                    Item {
                        anchors.fill: parent
                        visible: previewColumn.isArchive

                        Text {
                            id: archivePreviewTitle
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 8
                            text: "Archive contents"
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Text {
                            anchors.top: archivePreviewTitle.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 8
                            visible: previewColumn.directoryPreview.error !== ""
                            text: previewColumn.directoryPreview.error
                            color: Theme.error
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }

                        ListView {
                            id: archivePreviewList
                            anchors.top: archivePreviewTitle.bottom
                            anchors.topMargin: 8
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            visible: previewColumn.directoryPreview.error === ""
                            model: previewColumn.directoryPreview.entries || []
                            clip: true
                            spacing: 4

                            delegate: Text {
                                width: archivePreviewList.width - 12
                                text: modelData
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                elide: Text.ElideMiddle
                            }

                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        }
                    }

                    // Image/Video preview
                    Image {
                        id: visualPreview
                        anchors.fill: parent
                        anchors.margins: 8
                        visible: previewColumn.hasVisualPreview && !previewColumn.previewIsDir && !previewColumn.isPdf
                        source: previewColumn.visualSource
                        sourceSize: Qt.size(width, height)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        smooth: true
                    }

                    // PDF preview
                    Image {
                        id: pdfPreviewImage
                        anchors.fill: parent
                        anchors.margins: 8
                        visible: previewColumn.isPdf
                            && previewColumn.pdfPreviewAvailable
                            && previewColumn.pdfPreview.localPath !== ""
                            && previewColumn.pdfPreview.error === ""
                        source: previewColumn.pdfImageSource
                        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        smooth: true
                    }

                    MouseArea {
                        anchors.fill: parent
                        visible: pdfPreviewImage.visible
                        acceptedButtons: Qt.NoButton
                        onWheel: (wheel) => previewColumn.handlePdfWheel(wheel)
                    }

                    Row {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: 10
                        anchors.rightMargin: 10
                        spacing: 6
                        visible: pdfPreviewImage.visible && previewColumn.pdfPreview.pageCount > 1

                        Rectangle {
                            width: 26
                            height: 26
                            radius: 13
                            enabled: previewColumn.pdfPageIndex > 0
                            opacity: enabled ? 1 : 0.45
                            color: pdfPrevMouse.containsMouse
                                ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                            IconChevronUp {
                                anchors.centerIn: parent
                                size: 14
                                color: Theme.text
                            }

                            MouseArea {
                                id: pdfPrevMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: parent.enabled
                                onClicked: previewColumn.changePdfPage(-1)
                            }
                        }

                        Rectangle {
                            width: 26
                            height: 26
                            radius: 13
                            enabled: previewColumn.pdfPageIndex < previewColumn.pdfPreview.pageCount - 1
                            opacity: enabled ? 1 : 0.45
                            color: pdfNextMouse.containsMouse
                                ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                            IconChevronDown {
                                anchors.centerIn: parent
                                size: 14
                                color: Theme.text
                            }

                            MouseArea {
                                id: pdfNextMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                enabled: parent.enabled
                                onClicked: previewColumn.changePdfPage(1)
                            }
                        }
                    }

                    Text {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 10
                        visible: pdfPreviewImage.visible && previewColumn.pdfPageLabel !== ""
                        text: previewColumn.pdfPageLabel
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        visible: (previewColumn.hasVisualPreview && visualPreview.status === Image.Loading)
                            || (pdfPreviewImage.visible && pdfPreviewImage.status === Image.Loading)
                        color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.72)
                        radius: Theme.radiusMedium
                        width: 170
                        height: 40

                        Text {
                            anchors.centerIn: parent
                            text: previewColumn.isPdf ? "Rendering PDF..." : "Loading preview..."
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                        visible: (previewColumn.hasVisualPreview && visualPreview.status === Image.Error)
                            || (pdfPreviewImage.visible && pdfPreviewImage.status === Image.Error)

                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 64; height: 64
                            source: "image://icon/" + (previewColumn.fileProps.iconName || (previewColumn.isPdf ? "application-pdf" : "image-x-generic"))
                            sourceSize: Qt.size(64, 64)
                            asynchronous: false
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: previewColumn.isPdf ? "PDF preview could not be loaded" : "Preview could not be loaded"
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                        }
                    }

                    // Text preview
                    Flickable {
                        id: textPreviewFlick
                        anchors.fill: parent
                        anchors.margins: 6
                        visible: previewColumn.isText && !previewColumn.hasVisualPreview
                            && !previewColumn.previewIsDir && !previewColumn.isPdf && !previewColumn.isArchive
                        clip: true
                        interactive: true
                        boundsMovement: Flickable.StopAtBounds
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: Math.max(width, textArea.contentWidth)
                        contentHeight: Math.max(height, textArea.contentHeight)

                        TextEdit {
                            id: textArea
                            readOnly: true
                            selectByMouse: true
                            textFormat: previewColumn.textPreview.usesBat && previewColumn.textPreview.html !== ""
                                ? TextEdit.RichText
                                : TextEdit.PlainText
                            text: previewColumn.textPreview.error !== ""
                                ? previewColumn.textPreview.error
                                : (previewColumn.textPreview.isBinary
                                    ? "This file looks binary and cannot be previewed as text."
                                    : (previewColumn.textPreview.usesBat && previewColumn.textPreview.html !== ""
                                        ? previewColumn.textPreview.html
                                        : previewColumn.textPreview.content))
                            color: Theme.text
                            wrapMode: TextEdit.NoWrap
                            font.family: "monospace"
                            font.pointSize: Theme.fontSmall - 1
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                    }

                    Text {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        visible: textPreviewFlick.visible
                            && previewColumn.textPreview.error === ""
                            && !previewColumn.textPreview.isBinary
                            && !previewColumn.textPreview.usesBat
                            && !previewColumn.textHighlightAvailable
                        text: runtimeFeatures.installHint("textHighlight")
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: 10
                        visible: previewColumn.isPdf
                            && (!previewColumn.pdfPreviewAvailable || previewColumn.pdfPreview.error !== "")

                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 72; height: 72
                            source: "image://icon/" + (previewColumn.fileProps.iconName || "application-pdf")
                            sourceSize: Qt.size(72, 72)
                            asynchronous: false
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 220
                            text: previewColumn.pdfPreviewAvailable
                                ? "PDF preview is unavailable for this file"
                                : "PDF preview support is unavailable"
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 220
                            text: previewColumn.pdfPreview.error !== ""
                                ? previewColumn.pdfPreview.error
                                : runtimeFeatures.installHint("pdfPreview")
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    // Fallback: icon for non-previewable files
                    Column {
                        anchors.centerIn: parent
                        spacing: 10
                        visible: !previewColumn.previewIsDir && !previewColumn.isArchive
                            && !previewColumn.hasVisualPreview && !previewColumn.isText
                            && !previewColumn.isPdf && previewColumn.previewFilePath !== ""

                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 64; height: 64
                            source: previewColumn.fileProps.iconName
                                ? ("image://icon/" + previewColumn.fileProps.iconName)
                                : "image://icon/text-x-generic"
                            sourceSize: Qt.size(64, 64)
                            asynchronous: false
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 220
                            text: previewColumn.isAudio
                                ? "Audio preview is not available yet"
                                : (previewColumn.isVideo && !previewColumn.videoPreviewAvailable
                                    ? "Video preview support is unavailable"
                                    : "Preview not available")
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            horizontalAlignment: Text.AlignHCenter
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 220
                            text: previewColumn.isVideo && !previewColumn.videoPreviewAvailable
                                ? runtimeFeatures.installHint("videoPreview")
                                : "Open Quick Preview for a larger preview surface."
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    // Empty state
                    Text {
                        anchors.centerIn: parent
                        visible: previewColumn.previewFilePath === ""
                        text: "No selection"
                        color: Qt.rgba(Theme.subtext.r, Theme.subtext.g, Theme.subtext.b, 0.5)
                        font.pointSize: Theme.fontSmall
                    }
                }

                // Info bar at bottom
                Rectangle {
                    id: infoBar
                    width: parent.width
                    height: previewColumn.previewFilePath !== ""
                        ? Math.min(parent.height * 0.34, Math.max(54, infoBarContent.implicitHeight + 14))
                        : 0
                    visible: previewColumn.previewFilePath !== ""
                    color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.5)
                    border.width: 0

                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                    }

                    Flickable {
                        id: infoBarFlick
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        anchors.topMargin: 7
                        anchors.bottomMargin: 7
                        clip: true
                        interactive: contentHeight > height
                        boundsMovement: Flickable.StopAtBounds
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: width
                        contentHeight: infoBarContent.implicitHeight

                        Column {
                            id: infoBarContent
                            width: infoBarFlick.width
                            spacing: 4

                            Text {
                                width: parent.width
                                text: previewColumn.previewFileName
                                color: Theme.text
                                font.pointSize: Theme.fontSmall
                                font.bold: true
                                elide: Text.ElideMiddle
                            }

                            Text {
                                width: parent.width
                                text: {
                                    var parts = []
                                    parts.push(previewColumn.detailKind)
                                    if (previewColumn.fileProps.sizeText)
                                        parts.push(previewColumn.fileProps.sizeText)
                                    if (previewColumn.previewIsDir && previewColumn.fileProps.contentText)
                                        parts.push(previewColumn.fileProps.contentText)
                                    if (previewColumn.pdfPageLabel !== "")
                                        parts.push(previewColumn.pdfPageLabel)
                                    if (previewColumn.fileProps.modified)
                                        parts.push(previewColumn.fileProps.modified)
                                    return parts.join(" \u00b7 ")
                                }
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall - 1
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Text {
                                width: parent.width
                                visible: !!(previewColumn.fileProps.originalPath || previewColumn.fileProps.parentDir)
                                text: (previewColumn.fileProps.originalPath ? "Original Location: " : "Location: ")
                                    + (previewColumn.fileProps.originalPath || previewColumn.fileProps.parentDir || "")
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall - 1
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Text {
                                width: parent.width
                                visible: !previewColumn.previewIsDir && !!previewColumn.fileProps.contentText
                                text: "Contents: " + previewColumn.fileProps.contentText
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall - 1
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Text {
                                width: parent.width
                                visible: !!previewColumn.fileProps.deleted
                                text: "Deleted: " + previewColumn.fileProps.deleted
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall - 1
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Repeater {
                                model: previewColumn.metadataEntries

                                delegate: Text {
                                    width: infoBarContent.width
                                    text: modelData.label + ": " + modelData.value
                                    color: Theme.subtext
                                    font.pointSize: Theme.fontSmall - 1
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }
                            }

                            Text {
                                width: parent.width
                                visible: previewColumn.metadataHint !== ""
                                text: previewColumn.metadataHint
                                color: Theme.muted
                                font.pointSize: Theme.fontSmall - 1
                                font.italic: true
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Text {
                                width: parent.width
                                visible: previewColumn.isText && previewColumn.textPreview.truncated
                                text: "Showing a shortened text preview for quick browsing."
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall - 1
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }

                            Text {
                                width: parent.width
                                visible: (previewColumn.previewIsDir || previewColumn.isArchive) && previewColumn.directoryPreview.truncated
                                text: "Only the first items are shown here."
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall - 1
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }
                }
            }
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
