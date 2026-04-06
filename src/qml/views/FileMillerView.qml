import QtQuick
import QtQuick.Controls
import HyprFM

Item {
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
        var p = currentPath
        if (p.endsWith("/") && p.length > 1) p = p.slice(0, -1)
        var idx = p.lastIndexOf("/")
        return idx <= 0 ? "/" : p.substring(0, idx)
    }

    readonly property string currentDirName: {
        if (!currentPath || currentPath === "/") return "/"
        var p = currentPath
        if (p.endsWith("/") && p.length > 1) p = p.slice(0, -1)
        var idx = p.lastIndexOf("/")
        return idx < 0 ? p : p.substring(idx + 1)
    }

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

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: parentDelegate
                width: parentColumn.width
                height: 28

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
                            return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                        if (parentMa.containsMouse)
                            return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                        return "transparent"
                    }
                    border.color: parentDelegate.isCurrentDir ? Theme.accent : "transparent"
                    border.width: parentDelegate.isCurrentDir ? 1 : 0

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        spacing: 6

                        Image {
                            width: 16; height: 16
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://icon/" + parentDelegate.fileIconName
                            sourceSize: Qt.size(16, 16)
                            asynchronous: false
                        }

                        Text {
                            width: parent.width - 16 - parent.spacing - (parentDelegate.isDir ? 12 : 0) - parent.anchors.leftMargin - parent.anchors.rightMargin
                            anchors.verticalCenter: parent.verticalCenter
                            text: parentDelegate.fileName
                            color: parentDelegate.isCurrentDir ? Theme.text : Theme.subtext
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }

                        Text {
                            visible: parentDelegate.isDir
                            width: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: "\u25B8"
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                        }
                    }
                }

                MouseArea {
                    id: parentMa
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.interactionStarted()
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
                    currentColumn.clearSelection()
                    currentColumn.typeAheadBuffer = ""
                    typeAheadTimer.stop()
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

            function schedulePendingFocus() {
                if (focusScheduled) return
                focusScheduled = true
                Qt.callLater(function() {
                    focusScheduled = false
                    if (pendingFocusPath !== "")
                        focusPath(pendingFocusPath, pendingFocusReveal)
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
                height: 28

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
                            width: 18; height: 18
                            anchors.verticalCenter: parent.verticalCenter

                            Image {
                                anchors.fill: parent
                                source: "image://icon/" + currentDelegate.fileIconName
                                sourceSize: Qt.size(18, 18)
                                asynchronous: false
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
                            width: parent.width - 18 - (currentDelegate.isDir ? 12 : 0) - parent.spacing * (currentDelegate.isDir ? 2 : 1) - parent.anchors.leftMargin - parent.anchors.rightMargin
                            anchors.verticalCenter: parent.verticalCenter
                            text: currentDelegate.fileName
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }

                        Text {
                            visible: currentDelegate.isDir
                            width: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: "\u25B8"
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
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

            MouseArea {
                anchors.fill: parent
                z: -1
                acceptedButtons: Qt.RightButton
                onClicked: (mouse) => {
                    var mapped = mapToItem(null, mouse.x, mouse.y)
                    root.contextMenuRequested("", false, Qt.point(mapped.x, mapped.y))
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

            readonly property string _mime: fileProps.mimeType || ""
            readonly property bool isRemoteUri: previewFilePath !== "" && fileOps.isRemotePath(previewFilePath)
            readonly property bool isImage: !isRemoteUri && !previewIsDir && _mime.startsWith("image/")
            readonly property bool isVideo: !isRemoteUri && !previewIsDir && _mime.startsWith("video/")
            readonly property bool isAudio: !isRemoteUri && !previewIsDir && (_mime.startsWith("audio/") || false)
            readonly property bool hasVisualPreview: isImage || (isVideo && runtimeFeatures.ffmpegAvailable)
            readonly property string visualSource: {
                if (!hasVisualPreview || previewFilePath === "") return ""
                if (isVideo) return "image://thumbnail/" + previewFilePath
                return "file://" + previewFilePath
            }
            readonly property bool isText: {
                if (isRemoteUri || previewIsDir || isImage || isVideo || isAudio) return false
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
                if (previewFilePath === "") return ""
                var idx = previewFilePath.lastIndexOf("/")
                return idx >= 0 ? previewFilePath.substring(idx + 1) : previewFilePath
            }

            readonly property string detailKind: {
                if (previewIsDir) return "Folder"
                if (fileProps.mimeDescription) return fileProps.mimeDescription
                var ext = previewFileName.lastIndexOf(".") >= 0
                    ? previewFileName.substring(previewFileName.lastIndexOf(".") + 1).toUpperCase() : ""
                return ext !== "" ? ext + " file" : "File"
            }

            function refreshPreview() {
                if (previewFilePath === "") {
                    fileProps = ({})
                    textPreview = ({ content: "", truncated: false, isBinary: false, error: "" })
                    directoryPreview = ({ entries: [], truncated: false, error: "", count: 0 })
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

                if (previewIsDir)
                    directoryPreview = previewService.loadDirectoryPreview(previewFilePath)
                else
                    directoryPreview = ({ entries: [], truncated: false, error: "", count: 0 })
            }

            onPreviewFilePathChanged: refreshPreview()

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

                    // Image/Video preview
                    Image {
                        id: visualPreview
                        anchors.fill: parent
                        anchors.margins: 8
                        visible: previewColumn.hasVisualPreview && !previewColumn.previewIsDir
                        source: previewColumn.visualSource
                        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        smooth: true
                    }

                    // Text preview
                    Flickable {
                        id: textPreviewFlick
                        anchors.fill: parent
                        anchors.margins: 6
                        visible: previewColumn.isText && !previewColumn.hasVisualPreview && !previewColumn.previewIsDir
                        clip: true
                        interactive: true
                        boundsMovement: Flickable.StopAtBounds
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: Math.max(width, textArea.contentWidth)
                        contentHeight: Math.max(height, textArea.contentHeight)

                        TextEdit {
                            id: textArea
                            readOnly: true
                            selectByMouse: false
                            textFormat: previewColumn.textPreview.usesBat && previewColumn.textPreview.html !== ""
                                ? TextEdit.RichText
                                : TextEdit.PlainText
                            text: previewColumn.textPreview.error !== ""
                                ? previewColumn.textPreview.error
                                : (previewColumn.textPreview.isBinary
                                    ? "Binary file"
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

                    // Fallback: icon for non-previewable files
                    Column {
                        anchors.centerIn: parent
                        spacing: 8
                        visible: !previewColumn.previewIsDir && !previewColumn.hasVisualPreview
                            && !previewColumn.isText && previewColumn.previewFilePath !== ""

                        Image {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 64; height: 64
                            source: previewColumn.fileProps.iconName
                                ? ("image://icon/" + previewColumn.fileProps.iconName)
                                : "image://icon/text-x-generic"
                            sourceSize: Qt.size(64, 64)
                            asynchronous: false
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
                    height: previewColumn.previewFilePath !== "" ? infoBarContent.implicitHeight + 12 : 0
                    visible: previewColumn.previewFilePath !== ""
                    color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.5)
                    border.width: 0

                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                    }

                    Column {
                        id: infoBarContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 6
                        anchors.topMargin: 7
                        spacing: 2

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
                                if (previewColumn.fileProps.modified)
                                    parts.push(previewColumn.fileProps.modified)
                                return parts.join(" \u00b7 ")
                            }
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall - 1
                            elide: Text.ElideRight
                        }
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
