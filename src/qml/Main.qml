import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: "HyprFM"
    color: "transparent"

    // ── Sync fsModel when active tab changes; quit on last tab closed ───────
    Connections {
        target: tabModel
        function onActiveIndexChanged() {
            if (tabModel.activeTab)
                fsModel.setRootPath(tabModel.activeTab.currentPath)
        }
        function onLastTabClosed() {
            Qt.quit()
        }
    }

    Connections {
        target: tabModel.activeTab ?? null
        ignoreUnknownSignals: true
        function onCurrentPathChanged() {
            if (tabModel.activeTab)
                fsModel.setRootPath(tabModel.activeTab.currentPath)
        }
    }

    // Force initial load after QML is fully set up
    Component.onCompleted: {
        if (tabModel.activeTab) {
            fsModel.refresh()
        }
    }

    // ── Sidebar visibility (local property; config.sidebarVisible is read-only) ─
    property bool sidebarVisible: config.sidebarVisible

    // ── Selection state for StatusBar ────────────────────────────────────────
    property int currentSelectedCount: 0
    property string currentSelectedSize: ""

    function updateSelectionStatus() {
        var vm = tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
        var subView = null
        if (vm === "grid")          subView = fileViewContainer.gridViewItem
        else if (vm === "list")     subView = fileViewContainer.listViewItem
        else if (vm === "detailed") subView = fileViewContainer.detailedViewItem

        if (!subView || !subView.selectedIndices) {
            currentSelectedCount = 0
            currentSelectedSize = ""
            return
        }

        var indices = subView.selectedIndices
        currentSelectedCount = indices.length

        if (indices.length === 0) {
            currentSelectedSize = ""
            return
        }

        // Size display is omitted here as it requires per-file stat
        // (fsModel.data is not Q_INVOKABLE; count alone suffices for the status bar)
        currentSelectedSize = ""
    }

    // ── Helper: collect selected file paths from active view ─────────────────
    function getSelectedPaths() {
        var paths = []
        var view = fileViewContainer
        if (!view) return paths

        // Access the active sub-view's selectedIndices
        var subView = null
        var vm = tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
        if (vm === "grid")          subView = view.gridViewItem
        else if (vm === "list")     subView = view.listViewItem
        else if (vm === "detailed") subView = view.detailedViewItem

        if (!subView || !subView.selectedIndices) return paths

        var indices = subView.selectedIndices
        for (var i = 0; i < indices.length; i++) {
            var fp = fsModel.filePath(indices[i])
            if (fp !== "") paths.push(fp)
        }
        return paths
    }

    // ── Helper: list of all file paths in current directory (for preview cycling)
    function getDirectoryFiles() {
        var files = []
        var count = fsModel.rowCount()
        for (var i = 0; i < count; i++) {
            files.push(fsModel.filePath(i))
        }
        return files
    }

    // ── Rename dialog ───────────────────────────────────────────────────────
    property string renameTargetPath: ""

    Item {
        id: renameDialog
        anchors.fill: parent
        visible: false
        z: 1000

        function open() {
            visible = true
            renameBox.opacity = 0
            renameBox.scale = 0.88
            renameBox.yOffset = -8
            renameOpenAnim.start()
            renameField.forceActiveFocus()
        }
        function accept() {
            if (renameTargetPath !== "" && renameField.text.trim() !== "")
                fileOps.rename(renameTargetPath, renameField.text.trim())
            renameCloseAnim.start()
        }
        function reject() { renameCloseAnim.start() }

        ParallelAnimation {
            id: renameOpenAnim
            NumberAnimation {
                target: renameBox; property: "opacity"
                from: 0; to: 1; duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: renameBox; property: "scale"
                from: 0.88; to: 1; duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 0.8
            }
            NumberAnimation {
                target: renameBox; property: "yOffset"
                from: -8; to: 0; duration: 220
                easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            id: renameCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: renameBox; property: "opacity"
                    to: 0; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: renameBox; property: "scale"
                    to: 0.92; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: renameBox; property: "yOffset"
                    to: -4; duration: 120
                    easing.type: Easing.InCubic
                }
            }
            ScriptAction { script: renameDialog.visible = false }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: renameDialog.reject()
        }

        Item {
            id: renameBox
            width: 340
            height: renameContent.implicitHeight + 40
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: renameBox.yOffset }

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                radius: Theme.radiusMedium
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                border.width: 1
            }

            Column {
                id: renameContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12

                Text {
                    text: "Rename"
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                }

                Text {
                    text: "New name:"
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSmall
                }

                TextField {
                    id: renameField
                    width: parent.width
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    padding: 8
                    background: Rectangle {
                        color: Theme.surface
                        radius: Theme.radiusSmall
                        border.color: renameField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15)
                        border.width: 1
                    }
                    Keys.onReturnPressed: renameDialog.accept()
                    Keys.onEscapePressed: renameDialog.reject()
                }

                Item { width: 1; height: 4 }

                Row {
                    anchors.right: parent.right
                    spacing: 12

                    Text {
                        text: "Cancel"
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSmall
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: renameDialog.reject()
                        }
                    }

                    Rectangle {
                        width: okRenameText.implicitWidth + 24
                        height: 28
                        radius: Theme.radiusSmall
                        color: Theme.accent
                        Text {
                            id: okRenameText
                            text: "Rename"
                            color: Theme.mantle
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: renameDialog.accept()
                        }
                    }
                }
            }
        }
    }

    // ── New Folder dialog ───────────────────────────────────────────────────
    property string newItemParentPath: ""

    Item {
        id: newFolderDialog
        anchors.fill: parent
        visible: false
        z: 1000

        function open() {
            visible = true
            folderBox.opacity = 0
            folderBox.scale = 0.88
            folderBox.yOffset = -8
            folderOpenAnim.start()
            newFolderField.forceActiveFocus()
        }
        function accept() {
            if (newItemParentPath !== "" && newFolderField.text.trim() !== "")
                fileOps.createFolder(newItemParentPath, newFolderField.text.trim())
            folderCloseAnim.start()
        }
        function reject() { folderCloseAnim.start() }

        ParallelAnimation {
            id: folderOpenAnim
            NumberAnimation {
                target: folderBox; property: "opacity"
                from: 0; to: 1; duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: folderBox; property: "scale"
                from: 0.88; to: 1; duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 0.8
            }
            NumberAnimation {
                target: folderBox; property: "yOffset"
                from: -8; to: 0; duration: 220
                easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            id: folderCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: folderBox; property: "opacity"
                    to: 0; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: folderBox; property: "scale"
                    to: 0.92; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: folderBox; property: "yOffset"
                    to: -4; duration: 120
                    easing.type: Easing.InCubic
                }
            }
            ScriptAction { script: newFolderDialog.visible = false }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: newFolderDialog.reject()
        }

        Item {
            id: folderBox
            width: 340
            height: folderContent.implicitHeight + 40
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: folderBox.yOffset }

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                radius: Theme.radiusMedium
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                border.width: 1
            }

            Column {
                id: folderContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12

                Text {
                    text: "New Folder"
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                }

                Text {
                    text: "Folder name:"
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSmall
                }

                TextField {
                    id: newFolderField
                    width: parent.width
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    padding: 8
                    background: Rectangle {
                        color: Theme.surface
                        radius: Theme.radiusSmall
                        border.color: newFolderField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15)
                        border.width: 1
                    }
                    Keys.onReturnPressed: newFolderDialog.accept()
                    Keys.onEscapePressed: newFolderDialog.reject()
                }

                Item { width: 1; height: 4 }

                Row {
                    anchors.right: parent.right
                    spacing: 12

                    Text {
                        text: "Cancel"
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSmall
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: newFolderDialog.reject()
                        }
                    }

                    Rectangle {
                        width: okFolderText.implicitWidth + 24
                        height: 28
                        radius: Theme.radiusSmall
                        color: Theme.accent
                        Text {
                            id: okFolderText
                            text: "Create"
                            color: Theme.mantle
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: newFolderDialog.accept()
                        }
                    }
                }
            }
        }
    }

    // ── New File dialog ─────────────────────────────────────────────────────
    Item {
        id: newFileDialog
        anchors.fill: parent
        visible: false
        z: 1000

        function open() {
            visible = true
            fileBox.opacity = 0
            fileBox.scale = 0.88
            fileBox.yOffset = -8
            fileOpenAnim.start()
            newFileField.forceActiveFocus()
        }
        function accept() {
            if (newItemParentPath !== "" && newFileField.text.trim() !== "")
                fileOps.createFile(newItemParentPath, newFileField.text.trim())
            fileCloseAnim.start()
        }
        function reject() { fileCloseAnim.start() }

        ParallelAnimation {
            id: fileOpenAnim
            NumberAnimation {
                target: fileBox; property: "opacity"
                from: 0; to: 1; duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: fileBox; property: "scale"
                from: 0.88; to: 1; duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 0.8
            }
            NumberAnimation {
                target: fileBox; property: "yOffset"
                from: -8; to: 0; duration: 220
                easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            id: fileCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: fileBox; property: "opacity"
                    to: 0; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: fileBox; property: "scale"
                    to: 0.92; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: fileBox; property: "yOffset"
                    to: -4; duration: 120
                    easing.type: Easing.InCubic
                }
            }
            ScriptAction { script: newFileDialog.visible = false }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: newFileDialog.reject()
        }

        Item {
            id: fileBox
            width: 340
            height: fileContent.implicitHeight + 40
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: fileBox.yOffset }

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                radius: Theme.radiusMedium
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                border.width: 1
            }

            Column {
                id: fileContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12

                Text {
                    text: "New File"
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                }

                Text {
                    text: "File name:"
                    color: Theme.subtext
                    font.pixelSize: Theme.fontSmall
                }

                TextField {
                    id: newFileField
                    width: parent.width
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    padding: 8
                    background: Rectangle {
                        color: Theme.surface
                        radius: Theme.radiusSmall
                        border.color: newFileField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15)
                        border.width: 1
                    }
                    Keys.onReturnPressed: newFileDialog.accept()
                    Keys.onEscapePressed: newFileDialog.reject()
                }

                Item { width: 1; height: 4 }

                Row {
                    anchors.right: parent.right
                    spacing: 12

                    Text {
                        text: "Cancel"
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSmall
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: newFileDialog.reject()
                        }
                    }

                    Rectangle {
                        width: okFileText.implicitWidth + 24
                        height: 28
                        radius: Theme.radiusSmall
                        color: Theme.accent
                        Text {
                            id: okFileText
                            text: "Create"
                            color: Theme.mantle
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: newFileDialog.accept()
                        }
                    }
                }
            }
        }
    }

    // ── Context Menu ────────────────────────────────────────────────────────
    ContextMenu {
        id: contextMenu
        blurSource: mainContent

        onOpenRequested: (path) => fileOps.openFile(path)

        onCutRequested: (paths) => clipboard.cut(paths)

        onCopyRequested: (paths) => clipboard.copy(paths)

        onPasteRequested: (destPath) => {
            var wasCut = clipboard.isCut
            var items = clipboard.take()
            if (!items || items.length === 0) return
            if (wasCut)
                fileOps.moveFiles(items, destPath)
            else
                fileOps.copyFiles(items, destPath)
        }

        onCopyPathRequested: (path) => fileOps.copyPathToClipboard(path)

        onRenameRequested: (path) => {
            root.renameTargetPath = path
            var name = path.substring(path.lastIndexOf("/") + 1)
            renameField.text = name
            renameDialog.open()
        }

        onTrashRequested: (paths) => fileOps.trashFiles(paths)

        onDeleteRequested: (paths) => fileOps.deleteFiles(paths)

        onOpenInTerminalRequested: (path) => {
            fileOps.openInTerminal(path)
        }

        onNewFolderRequested: (parentPath) => {
            root.newItemParentPath = parentPath
            newFolderField.text = ""
            newFolderDialog.open()
        }

        onNewFileRequested: (parentPath) => {
            root.newItemParentPath = parentPath
            newFileField.text = ""
            newFileDialog.open()
        }

        onSelectAllRequested: fileViewContainer.selectAll()

        onPropertiesRequested: (path) => {
            // Basic: open file manager properties or show info
            fileOps.openFile(path)
        }
    }

    // ── Keyboard Shortcuts ──────────────────────────────────────────────────

    // Tab management
    Shortcut {
        sequence: config.shortcut("new_tab")
        onActivated: tabModel.addTab()
    }

    Shortcut {
        sequence: config.shortcut("close_tab")
        onActivated: {
            if (tabModel.count > 1) tabModel.closeTab(tabModel.activeIndex)
        }
    }

    Shortcut {
        sequence: config.shortcut("reopen_tab")
        onActivated: tabModel.reopenClosedTab()
    }

    // Navigation
    Shortcut {
        sequence: config.shortcut("back")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.goBack() }
    }

    Shortcut {
        sequence: "Backspace"
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.goBack() }
    }

    Shortcut {
        sequence: config.shortcut("forward")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.goForward() }
    }

    Shortcut {
        sequence: config.shortcut("parent")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.goUp() }
    }

    // Toggle hidden files
    Shortcut {
        sequence: config.shortcut("toggle_hidden")
        onActivated: fsModel.showHidden = !fsModel.showHidden
    }

    // Toggle path bar focus (Ctrl+L-like)
    Shortcut {
        sequence: config.shortcut("path_bar")
        onActivated: toolbar.startEditing()
    }

    // Toggle sidebar
    Shortcut {
        sequence: config.shortcut("toggle_sidebar")
        onActivated: root.sidebarVisible = !root.sidebarVisible
    }

    // View mode switching
    Shortcut {
        sequence: config.shortcut("grid_view")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.viewMode = "grid" }
    }

    Shortcut {
        sequence: config.shortcut("list_view")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.viewMode = "list" }
    }

    Shortcut {
        sequence: config.shortcut("detailed_view")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.viewMode = "detailed" }
    }

    // File operations
    Shortcut {
        sequence: config.shortcut("copy")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length > 0) clipboard.copy(paths)
        }
    }

    Shortcut {
        sequence: config.shortcut("cut")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length > 0) clipboard.cut(paths)
        }
    }

    Shortcut {
        sequence: config.shortcut("paste")
        onActivated: {
            if (!clipboard.hasContent) return
            var dest = tabModel.activeTab ? tabModel.activeTab.currentPath : ""
            if (dest === "") return
            var wasCut = clipboard.isCut
            var items = clipboard.take()
            if (!items || items.length === 0) return
            if (wasCut)
                fileOps.moveFiles(items, dest)
            else
                fileOps.copyFiles(items, dest)
        }
    }

    Shortcut {
        sequence: config.shortcut("trash")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length > 0) fileOps.trashFiles(paths)
        }
    }

    Shortcut {
        sequence: config.shortcut("select_all")
        onActivated: fileViewContainer.selectAll()
    }

    // Quick preview (spacebar)
    Shortcut {
        sequence: "Space"
        onActivated: {
            if (quickPreview.active) {
                quickPreview.active = false
                return
            }
            var paths = getSelectedPaths()
            if (paths.length === 0) return
            quickPreview.filePath = paths[0]
            quickPreview.directoryFiles = getDirectoryFiles()
            quickPreview.active = true
            quickPreview.forceActiveFocus()
        }
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    ColumnLayout {
        id: mainContent
        anchors.fill: parent
        spacing: 0

        // Tab bar
        FileTabBar {
            Layout.fillWidth: true
        }

        // Toolbar with breadcrumb and view mode toggle
        Toolbar {
            id: toolbar
            Layout.fillWidth: true
            activeTab: tabModel.activeTab
        }

        // Main content area
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Sidebar with bookmarks
            Sidebar {
                width: config.sidebarWidth
                Layout.fillHeight: true
                visible: root.sidebarVisible
                currentPath: tabModel.activeTab ? tabModel.activeTab.currentPath : ""
                onBookmarkClicked: (path) => {
                    if (tabModel.activeTab) tabModel.activeTab.navigateTo(path)
                }
            }

            // File view (semi-transparent — Hyprland compositor blurs behind this)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.65)

            FileViewContainer {
                id: fileViewContainer
                anchors.fill: parent
                fileModel: fsModel
                viewMode: tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
                currentPath: tabModel.activeTab ? tabModel.activeTab.currentPath : ""

                onFileActivated: (filePath, isDirectory) => {
                    if (isDirectory) {
                        if (tabModel.activeTab) tabModel.activeTab.navigateTo(filePath)
                    } else {
                        fileOps.openFile(filePath)
                    }
                }

                onSelectionChanged: root.updateSelectionStatus()

                onContextMenuRequested: (filePath, isDirectory, position) => {
                    var currentDir = tabModel.activeTab ? tabModel.activeTab.currentPath : ""
                    contextMenu.targetPath = filePath !== "" ? filePath : currentDir
                    contextMenu.targetIsDir = filePath !== "" ? isDirectory : true
                    contextMenu.isEmptySpace = (filePath === "")
                    var sel = getSelectedPaths()
                    contextMenu.selectedPaths = (sel.length > 1) ? sel : (filePath !== "" ? [filePath] : [])
                    contextMenu.popup(position.x, position.y)
                }
            }
            } // Rectangle wrapper
        }

        // Status bar
        StatusBar {
            id: statusBar
            Layout.fillWidth: true
            itemCount: fsModel.fileCount + fsModel.folderCount
            folderCount: fsModel.folderCount
            selectedCount: root.currentSelectedCount
            selectedSize: root.currentSelectedSize
        }
    }

    // ── Mouse back/forward button support ────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        z: -100
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        propagateComposedEvents: true
        onClicked: (mouse) => {
            if (mouse.button === Qt.BackButton && tabModel.activeTab)
                tabModel.activeTab.goBack()
            else if (mouse.button === Qt.ForwardButton && tabModel.activeTab)
                tabModel.activeTab.goForward()
        }
    }

    // ── Quick Preview overlay (on top of everything) ─────────────────────────
    QuickPreview {
        id: quickPreview
        anchors.fill: parent
        z: 100
        onClosed: quickPreview.active = false
    }

    // ── Toast notifications ──────────────────────────────────────────────────
    Toast {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
    }

    Connections {
        target: fileOps
        function onOperationFinished(success, error) {
            if (success)
                toast.show("Operation completed successfully", "success")
            else
                toast.show(error || "Operation failed", "error")
        }
    }
}
