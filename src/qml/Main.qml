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
    color: Theme.base

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

    Dialog {
        id: renameDialog
        title: "Rename"
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        width: 360

        background: Rectangle {
            color: Theme.mantle
            radius: Theme.radiusMedium
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Text {
                text: "New name:"
                color: Theme.text
                font.pixelSize: Theme.fontNormal
            }

            TextField {
                id: renameField
                Layout.fillWidth: true
                color: Theme.text
                background: Rectangle {
                    color: Theme.surface
                    radius: Theme.radiusSmall
                    border.color: renameField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.2)
                    border.width: 1
                }
            }
        }

        onAccepted: {
            if (renameTargetPath !== "" && renameField.text.trim() !== "") {
                fileOps.rename(renameTargetPath, renameField.text.trim())
            }
        }
    }

    // ── New Folder dialog ───────────────────────────────────────────────────
    property string newItemParentPath: ""

    Dialog {
        id: newFolderDialog
        title: "New Folder"
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        width: 360

        background: Rectangle {
            color: Theme.mantle
            radius: Theme.radiusMedium
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Text {
                text: "Folder name:"
                color: Theme.text
                font.pixelSize: Theme.fontNormal
            }

            TextField {
                id: newFolderField
                Layout.fillWidth: true
                color: Theme.text
                background: Rectangle {
                    color: Theme.surface
                    radius: Theme.radiusSmall
                    border.color: newFolderField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.2)
                    border.width: 1
                }
            }
        }

        onAccepted: {
            if (newItemParentPath !== "" && newFolderField.text.trim() !== "") {
                fileOps.createFolder(newItemParentPath, newFolderField.text.trim())
            }
        }
    }

    // ── New File dialog ─────────────────────────────────────────────────────
    Dialog {
        id: newFileDialog
        title: "New File"
        standardButtons: Dialog.Ok | Dialog.Cancel
        anchors.centerIn: parent
        width: 360

        background: Rectangle {
            color: Theme.mantle
            radius: Theme.radiusMedium
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
            border.width: 1
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Text {
                text: "File name:"
                color: Theme.text
                font.pixelSize: Theme.fontNormal
            }

            TextField {
                id: newFileField
                Layout.fillWidth: true
                color: Theme.text
                background: Rectangle {
                    color: Theme.surface
                    radius: Theme.radiusSmall
                    border.color: newFileField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.2)
                    border.width: 1
                }
            }
        }

        onAccepted: {
            if (newItemParentPath !== "" && newFileField.text.trim() !== "") {
                fileOps.createFile(newItemParentPath, newFileField.text.trim())
            }
        }
    }

    // ── Context Menu ────────────────────────────────────────────────────────
    ContextMenu {
        id: contextMenu

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

            // File view
            FileViewContainer {
                id: fileViewContainer
                Layout.fillWidth: true
                Layout.fillHeight: true
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
                    contextMenu.targetPath = filePath
                    contextMenu.targetIsDir = isDirectory
                    contextMenu.isEmptySpace = (filePath === "")
                    var sel = getSelectedPaths()
                    contextMenu.selectedPaths = (sel.length > 1) ? sel : (filePath !== "" ? [filePath] : [])
                    contextMenu.popup(position.x, position.y)
                }
            }
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
