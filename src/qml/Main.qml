import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import HyprFM
import Quill as Q

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: "HyprFM"
    color: "transparent"

    property bool primaryPaneIsRecents: false
    property bool secondaryPaneIsRecents: false
    property bool primaryPaneSearchMode: false
    property bool secondaryPaneSearchMode: false
    property bool primaryPaneFilterPanelOpen: false
    property bool secondaryPaneFilterPanelOpen: false
    readonly property bool isRecentsView: activePane === "secondary"
        ? secondaryPaneIsRecents
        : primaryPaneIsRecents
    property var deleteConfirmPaths: []
    property var transferConflictItems: []
    property var transferResolvedItems: []
    property int transferConflictIndex: -1
    property bool transferMoveOperation: false
    property bool transferClearClipboardOnSuccess: false
    property string transferDestinationPath: ""
    property var transferReservedTargets: ({})
    property bool paneFocusScheduled: false
    readonly property string unifiedTrashPath: "trash:///"
    readonly property bool isTrashView: fileOps.isTrashPath(panePath(activePane))

    // ── Sync fsModel when active tab changes; quit on last tab closed ───────
    Connections {
        target: tabModel
        function onActiveIndexChanged() {
            if (tabModel.activeTab) {
                root.activePane = "primary"
                root.primaryPaneIsRecents = false
                root.secondaryPaneIsRecents = false
                root.clearPaneSearch("primary")
                root.clearPaneSearch("secondary")
                fsModel.setRootPath(tabModel.activeTab.currentPath)
                splitFsModel.setRootPath(tabModel.activeTab.secondaryCurrentPath)
                root.applyActiveTabSort()
            }
        }
        function onLastTabClosed() {
            Qt.quit()
        }
    }

    Connections {
        target: tabModel.activeTab ?? null
        ignoreUnknownSignals: true
        function onCurrentPathChanged() {
            if (tabModel.activeTab) {
                fsModel.setRootPath(tabModel.activeTab.currentPath)
                root.setPaneRecents("primary", false)
                root.clearPaneSearch("primary")
            }
        }
        function onSecondaryCurrentPathChanged() {
            if (tabModel.activeTab) {
                splitFsModel.setRootPath(tabModel.activeTab.secondaryCurrentPath)
                root.setPaneRecents("secondary", false)
                root.clearPaneSearch("secondary")
            }
        }
        function onSortChanged() {
            root.applyActiveTabSort()
        }
        function onSplitViewEnabledChanged() {
            if (!tabModel.activeTab || tabModel.activeTab.splitViewEnabled)
                return

            if (root.activePane === "secondary")
                root.activePane = "primary"
            root.updateSelectionStatus()
        }
    }

    // Force initial load after QML is fully set up
    Component.onCompleted: {
        if (tabModel.activeTab) {
            fsModel.setRootPath(tabModel.activeTab.currentPath)
            fsModel.refresh()
            splitFsModel.setRootPath(tabModel.activeTab.secondaryCurrentPath)
            splitFsModel.refresh()
            root.applyActiveTabSort()
        }

        // Bridge HyprFM theme into Quill theme singleton
        Q.Theme.background = Qt.binding(() => Theme.base)
        Q.Theme.backgroundAlt = Qt.binding(() => Theme.mantle)
        Q.Theme.backgroundDeep = Qt.binding(() => Theme.crust)
        Q.Theme.surface0 = Qt.binding(() => Theme.surface)
        Q.Theme.surface1 = Qt.binding(() => Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1))
        Q.Theme.surface2 = Qt.binding(() => Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15))
        Q.Theme.textPrimary = Qt.binding(() => Theme.text)
        Q.Theme.textSecondary = Qt.binding(() => Theme.subtext)
        Q.Theme.textTertiary = Qt.binding(() => Theme.muted)
        Q.Theme.primary = Qt.binding(() => Theme.accent)
        Q.Theme.success = Qt.binding(() => Theme.success)
        Q.Theme.warning = Qt.binding(() => Theme.warning)
        Q.Theme.error = Qt.binding(() => Theme.error)
        Q.Theme.radiusSm = Qt.binding(() => Theme.radiusSmall)
        Q.Theme.radius = Qt.binding(() => Theme.radiusMedium)
        Q.Theme.radiusLg = Qt.binding(() => Theme.radiusLarge)
        Q.Theme.fontFamily = Qt.application.font.family
        Q.Theme.fontSizeSmall = Qt.binding(() => Theme.fontSmall)
        Q.Theme.fontSize = Qt.binding(() => Theme.fontNormal)
        Q.Theme.fontSizeLarge = Qt.binding(() => Theme.fontLarge)

        root.scheduleActivePaneFocus()
    }

    onActiveChanged: {
        if (active)
            root.scheduleActivePaneFocus()
    }

    // ── Sidebar visibility (local property; config.sidebarVisible is read-only) ─
    property bool sidebarVisible: config.sidebarVisible

    // ── Search state ──────────────────────────────────────────────────────────
    property var debounceTimer: null
    property string debouncePane: "primary"
    property string activePane: "primary"
    readonly property bool searchMode: paneSearchMode(activePane)

    // ── Selection state for StatusBar ────────────────────────────────────────
    property int currentSelectedCount: 0
    property string currentSelectedSize: ""

    function splitViewEnabled() {
        return tabModel.activeTab ? tabModel.activeTab.splitViewEnabled : false
    }

    function paneBaseModel(pane) {
        return pane === "secondary" ? splitFsModel : fsModel
    }

    function panePath(pane) {
        if (!tabModel.activeTab)
            return fsModel.homePath()

        return pane === "secondary"
            ? tabModel.activeTab.secondaryCurrentPath
            : tabModel.activeTab.currentPath
    }

    function paneIsRecents(pane) {
        return pane === "secondary" ? secondaryPaneIsRecents : primaryPaneIsRecents
    }

    function setPaneRecents(pane, enabled) {
        if (pane === "secondary")
            secondaryPaneIsRecents = enabled
        else
            primaryPaneIsRecents = enabled
    }

    function searchProxyForPane(pane) {
        return pane === "secondary" ? splitSearchProxy : searchProxy
    }

    function searchResultsForPane(pane) {
        return pane === "secondary" ? splitSearchResults : searchResults
    }

    function searchServiceForPane(pane) {
        return pane === "secondary" ? splitSearchService : searchService
    }

    function paneSearchMode(pane) {
        return pane === "secondary" ? secondaryPaneSearchMode : primaryPaneSearchMode
    }

    function setPaneSearchMode(pane, enabled) {
        if (pane === "secondary")
            secondaryPaneSearchMode = enabled
        else
            primaryPaneSearchMode = enabled
    }

    function paneFilterPanelOpen(pane) {
        return pane === "secondary" ? secondaryPaneFilterPanelOpen : primaryPaneFilterPanelOpen
    }

    function setPaneFilterPanelOpen(pane, enabled) {
        if (pane === "secondary")
            secondaryPaneFilterPanelOpen = enabled
        else
            primaryPaneFilterPanelOpen = enabled
    }

    function clearPaneDebounce(pane) {
        if (debounceTimer && debouncePane === pane) {
            debounceTimer.destroy()
            debounceTimer = null
        }
    }

    function clearPaneSearch(pane) {
        clearPaneDebounce(pane)
        setPaneSearchMode(pane, false)
        setPaneFilterPanelOpen(pane, false)
        searchServiceForPane(pane).cancelSearch()
        searchResultsForPane(pane).clear()
        searchProxyForPane(pane).clearSearch()
    }

    function paneModel(pane) {
        if (root.paneIsRecents(pane))
            return recentFiles

        if (root.paneSearchMode(pane))
            return searchProxyForPane(pane)

        return paneBaseModel(pane)
    }

    function filePathFromModel(model, row) {
        if (!model || row < 0)
            return ""

        if (model.filePath)
            return model.filePath(row)

        return model.data(model.index(row, 0), 258 /* FilePathRole */) || ""
    }

    function fileViewForPane(pane) {
        if (pane === "secondary")
            return secondaryPaneLoader.item ? secondaryPaneLoader.item.fileView : null

        return primaryFileViewContainer
    }

    function activeFileView() {
        return fileViewForPane(activePane)
    }

    function focusPathInPane(pane, path, reveal) {
        var view = fileViewForPane(pane)
        if (view && view.focusPath)
            view.focusPath(path, reveal)
    }

    function subViewFor(view) {
        if (!view)
            return null

        var vm = tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
        if (vm === "grid") return view.gridViewItem
        if (vm === "list") return view.listViewItem
        return view.detailedViewItem
    }

    function activeSubView() {
        return subViewFor(activeFileView())
    }

    function reservedTargetNames() {
        var names = []
        for (var path in transferReservedTargets) {
            if (!transferReservedTargets[path])
                continue
            var slashIndex = path.lastIndexOf("/")
            names.push(slashIndex >= 0 ? path.substring(slashIndex + 1) : path)
        }
        return names
    }

    function shouldFocusActivePane() {
        return root.active
            && !root.searchMode
            && !renameDialog.visible
            && !newFolderDialog.visible
            && !newFileDialog.visible
            && !conflictDialog.visible
            && !deleteConfirmDialog.visible
            && !emptyTrashConfirmDialog.visible
            && !quickPreview.active
    }

    function scheduleActivePaneFocus() {
        if (paneFocusScheduled)
            return

        paneFocusScheduled = true
        Qt.callLater(function() {
            paneFocusScheduled = false
            if (!root.shouldFocusActivePane())
                return

            var subView = root.activeSubView()
            if (subView)
                subView.forceActiveFocus()
        })
    }

    function setActivePane(pane) {
        var nextPane = pane
        if (nextPane === "secondary" && !splitViewEnabled())
            nextPane = "primary"

        if (activePane === nextPane)
            return

        activePane = nextPane
        root.updateSelectionStatus()
        root.scheduleActivePaneFocus()
    }

    function navigatePaneTo(pane, path) {
        if (!tabModel.activeTab || !path)
            return

        root.setPaneRecents(pane, false)
        root.clearPaneSearch(pane)
        if (pane === "secondary" && splitViewEnabled())
            tabModel.activeTab.navigateSecondaryTo(path)
        else
            tabModel.activeTab.navigateTo(path)
        root.scheduleActivePaneFocus()
    }

    function navigateActivePaneTo(path) {
        navigatePaneTo(activePane, path)
    }

    function resetTransferConflictState() {
        transferConflictItems = []
        transferResolvedItems = []
        transferConflictIndex = -1
        transferMoveOperation = false
        transferClearClipboardOnSuccess = false
        transferDestinationPath = ""
        transferReservedTargets = ({})
    }

    function executeTransferOperation(items, moveOperation, clearClipboardOnSuccess) {
        if (!items || items.length === 0)
            return

        if (clearClipboardOnSuccess) {
            fileOps.operationFinished.connect(function(success) {
                fileOps.operationFinished.disconnect(arguments.callee)
                if (success)
                    clipboard.clear()
            })
        }

        if (moveOperation)
            undoManager.moveResolvedItems(items)
        else
            undoManager.copyResolvedItems(items)
    }

    function openTransferConflict(index) {
        if (index < 0 || index >= transferConflictItems.length) {
            var items = transferResolvedItems.slice()
            var moveOperation = transferMoveOperation
            var clearClipboard = transferClearClipboardOnSuccess
            resetTransferConflictState()
            conflictDialog.close()
            executeTransferOperation(items, moveOperation, clearClipboard)
            return
        }

        transferConflictIndex = index
        var item = transferConflictItems[index]
        conflictRenameField.text = fileOps.uniqueNameForDestination(
            transferDestinationPath,
            item.sourceName,
            reservedTargetNames()
        )
        conflictErrorText.text = ""
        conflictDialog.currentItem = item
        conflictDialog.open()
    }

    function beginTransfer(paths, destinationPath, moveOperation, clearClipboardOnSuccess) {
        if (!paths || paths.length === 0 || !destinationPath)
            return

        var plan = fileOps.transferPlan(paths, destinationPath)
        if (!plan || plan.length === 0)
            return

        var resolved = []
        var conflicts = []
        var reserved = ({})

        for (var i = 0; i < plan.length; ++i) {
            var item = plan[i]
            var targetPath = item.targetPath
            var hasReservedConflict = reserved[targetPath] === true
            if (item.targetExists || item.samePath || hasReservedConflict) {
                conflicts.push(item)
                continue
            }

            reserved[targetPath] = true
            resolved.push({
                sourcePath: item.sourcePath,
                targetPath: item.targetPath,
                overwrite: false
            })
        }

        if (conflicts.length === 0) {
            executeTransferOperation(resolved, moveOperation, clearClipboardOnSuccess)
            return
        }

        transferResolvedItems = resolved
        transferConflictItems = conflicts
        transferConflictIndex = -1
        transferMoveOperation = moveOperation
        transferClearClipboardOnSuccess = clearClipboardOnSuccess
        transferDestinationPath = destinationPath
        transferReservedTargets = reserved
        openTransferConflict(0)
    }

    function resolveTransferConflict(action) {
        if (transferConflictIndex < 0 || transferConflictIndex >= transferConflictItems.length)
            return

        var item = transferConflictItems[transferConflictIndex]
        if (action === "overwrite") {
            if (item.samePath) {
                conflictErrorText.text = "Cannot overwrite an item with itself"
                return
            }

            transferReservedTargets[item.targetPath] = true
            transferResolvedItems = transferResolvedItems.concat([{ sourcePath: item.sourcePath, targetPath: item.targetPath, overwrite: true }])
        } else if (action === "rename") {
            var name = conflictRenameField.text.trim()
            if (name === "" || name === "." || name === ".." || name.indexOf("/") >= 0) {
                conflictErrorText.text = "Enter a valid file name"
                return
            }

            var targetPath = transferDestinationPath + "/" + name
            if (transferReservedTargets[targetPath] || fileOps.pathExists(targetPath) || targetPath === item.sourcePath) {
                conflictErrorText.text = "That name already exists"
                return
            }

            transferReservedTargets[targetPath] = true
            transferResolvedItems = transferResolvedItems.concat([{ sourcePath: item.sourcePath, targetPath: targetPath, overwrite: false }])
        }

        var nextIndex = transferConflictIndex + 1
        if (nextIndex >= transferConflictItems.length) {
            openTransferConflict(nextIndex)
            return
        }

        transferConflictIndex = nextIndex
        var nextItem = transferConflictItems[nextIndex]
        conflictDialog.currentItem = nextItem
        conflictRenameField.text = fileOps.uniqueNameForDestination(
            transferDestinationPath,
            nextItem.sourceName,
            reservedTargetNames()
        )
        conflictErrorText.text = ""
        conflictRenameField.forceActiveFocus()
    }

    function cancelTransferConflicts() {
        if (conflictDialog.visible)
            conflictDialog.close()
        else {
            resetTransferConflictState()
            scheduleActivePaneFocus()
        }
    }

    function goActivePaneBack() {
        if (!tabModel.activeTab)
            return

        if (activePane === "secondary" && splitViewEnabled())
            tabModel.activeTab.secondaryGoBack()
        else
            tabModel.activeTab.goBack()
    }

    function goActivePaneForward() {
        if (!tabModel.activeTab)
            return

        if (activePane === "secondary" && splitViewEnabled())
            tabModel.activeTab.secondaryGoForward()
        else
            tabModel.activeTab.goForward()
    }

    function goActivePaneUp() {
        if (!tabModel.activeTab || root.paneIsRecents(activePane))
            return

        var currentPath = panePath(activePane)
        if (currentPath.startsWith("trash:///")) {
            var normalized = currentPath.length > 9 && currentPath.endsWith("/")
                ? currentPath.slice(0, -1)
                : currentPath
            if (normalized === unifiedTrashPath.slice(0, -1) || normalized === unifiedTrashPath)
                return

            var slashIndex = normalized.lastIndexOf("/")
            var parentPath = slashIndex <= 8 ? unifiedTrashPath : normalized.substring(0, slashIndex)
            root.navigateActivePaneTo(parentPath)
            return
        }

        if (activePane === "secondary" && splitViewEnabled())
            tabModel.activeTab.secondaryGoUp()
        else
            tabModel.activeTab.goUp()
    }

    function toggleSplitView() {
        if (!tabModel.activeTab)
            return

        var enable = !tabModel.activeTab.splitViewEnabled
        if (enable) {
            root.clearPaneSearch("secondary")
            root.setPaneRecents("secondary", false)
            tabModel.activeTab.resetSecondaryTo(tabModel.activeTab.currentPath)
        }

        if (!enable) {
            root.clearPaneSearch("secondary")
            root.setPaneRecents("secondary", false)
            if (activePane === "secondary")
                activePane = "primary"
        }

        tabModel.activeTab.splitViewEnabled = enable
        root.updateSelectionStatus()
    }

    function activePaneCanGoBack() {
        if (!tabModel.activeTab)
            return false

        return activePane === "secondary" && splitViewEnabled()
            ? tabModel.activeTab.secondaryCanGoBack
            : tabModel.activeTab.canGoBack
    }

    function activePaneCanGoForward() {
        if (!tabModel.activeTab)
            return false

        return activePane === "secondary" && splitViewEnabled()
            ? tabModel.activeTab.secondaryCanGoForward
            : tabModel.activeTab.canGoForward
    }

    function activeItemCount() {
        if (root.paneIsRecents(activePane))
            return recentFiles.count
        if (root.paneSearchMode(activePane))
            return searchProxyForPane(activePane).rowCount()

        var model = paneBaseModel(activePane)
        return model.fileCount + model.folderCount
    }

    function activeFolderCount() {
        if (root.paneIsRecents(activePane) || root.paneSearchMode(activePane))
            return 0

        return paneBaseModel(activePane).folderCount
    }

    function applyActiveTabSort() {
        if (!tabModel.activeTab)
            return

        fsModel.sortByColumn(tabModel.activeTab.sortBy, tabModel.activeTab.sortAscending)
        splitFsModel.sortByColumn(tabModel.activeTab.sortBy, tabModel.activeTab.sortAscending)
    }

    function updateSelectionStatus() {
        var subView = activeSubView()

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
    function getSelectedPaths(pane) {
        var paths = []
        var targetPane = pane || activePane
        var view = fileViewForPane(targetPane)
        if (!view) return paths

        var subView = subViewFor(view)
        var model = paneModel(targetPane)

        if (!subView || !subView.selectedIndices || !model) return paths

        var indices = subView.selectedIndices
        for (var i = 0; i < indices.length; i++) {
            var fp = filePathFromModel(model, indices[i])
            if (fp !== "") paths.push(fp)
        }
        return paths
    }

    // ── Helper: list of all file paths in current directory (for preview cycling)
    function getDirectoryFiles() {
        var files = []
        var activeModel = paneModel(activePane)
        var count = activeModel.rowCount()
        for (var i = 0; i < count; i++) {
            var fp = filePathFromModel(activeModel, i)
            if (fp !== "")
                files.push(fp)
        }
        return files
    }

    // ── Search helpers ────────────────────────────────────────────────────────
    function openSearch() {
        setPaneRecents(activePane, false)
        setPaneSearchMode(activePane, true)
    }

    function closeSearch(pane) {
        clearPaneSearch(pane || activePane)
    }

    function handleSearchQuery(query) {
        var pane = activePane
        var proxy = searchProxyForPane(pane)
        var results = searchResultsForPane(pane)
        var service = searchServiceForPane(pane)

        proxy.searchQuery = query
        if (debounceTimer) debounceTimer.destroy()
        debounceTimer = null

        if (query === "") {
            service.cancelSearch()
            results.clear()
            return
        }

        debouncePane = pane
        debounceTimer = Qt.createQmlObject(
            'import QtQuick; Timer { interval: 500; running: true; repeat: false }',
            root
        )
        debounceTimer.triggered.connect(function() {
            root.triggerRecursiveSearch(pane, query)
            debounceTimer = null
        })
    }

    function triggerRecursiveSearch(pane, query) {
        var targetPane = pane || activePane
        var targetQuery = query !== undefined ? query : searchProxyForPane(targetPane).searchQuery
        if (targetQuery === "") return
        searchServiceForPane(targetPane).startSearch(
            panePath(targetPane),
            targetQuery,
            fsModel.showHidden
        )
    }

    function handleSearchEnter() {
        var query = searchProxyForPane(activePane).searchQuery
        if (query === "") return
        clearPaneDebounce(activePane)
        searchServiceForPane(activePane).startSearch(
            panePath(activePane),
            query,
            fsModel.showHidden
        )
    }

    function selectFirstSearchResult(pane) {
        var targetPane = pane || activePane
        if (!paneSearchMode(targetPane) || searchProxyForPane(targetPane).rowCount() === 0)
            return

        var subView = subViewFor(fileViewForPane(targetPane))
        if (subView && subView.selectedIndices !== undefined) {
            subView.selectedIndices = [0]
            if (targetPane === activePane)
                subView.forceActiveFocus()
        }
    }

    Connections {
        target: searchService
        function onSearchFinished() { root.selectFirstSearchResult("primary") }
    }

    Connections {
        target: splitSearchService
        function onSearchFinished() { root.selectFirstSearchResult("secondary") }
    }

    // ── Rename dialog ───────────────────────────────────────────────────────
    property string renameTargetPath: ""

    Item {
        id: renameDialog
        anchors.fill: parent
        visible: false
        z: 1000

        function open() {
            renameErrorText.text = ""
            visible = true
            renameBox.opacity = 0
            renameBox.scale = 0.88
            renameBox.yOffset = -8
            renameOpenAnim.start()
            Qt.callLater(function() {
                renameField.inputItem.forceActiveFocus()
                renameField.inputItem.selectAll()
            })
            renameField.forceActiveFocus()
        }
        function accept() {
            var name = renameField.text.trim()
            if (renameTargetPath === "" || name === "") return
            var parentDir = renameTargetPath.substring(0, renameTargetPath.lastIndexOf("/"))
            if (fileOps.pathExists(parentDir + "/" + name)) {
                renameErrorText.text = "\"" + name + "\" already exists"
                return
            }
            undoManager.rename(renameTargetPath, name)
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
            height: renameCard.implicitHeight
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: renameBox.yOffset }

            Q.Card {
                id: renameCard
                anchors.fill: parent
                title: "Rename"
                padding: 20
                color: Theme.mantle
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)

                Q.TextField {
                    id: renameField
                    Layout.fillWidth: true
                    autoFocus: true
                    variant: "filled"
                    placeholder: "Enter new name"
                    onTextChanged: renameErrorText.text = ""
                    Keys.onReturnPressed: renameDialog.accept()
                    Keys.onEscapePressed: renameDialog.reject()
                }

                Text {
                    id: renameErrorText
                    Layout.fillWidth: true
                    visible: text !== ""
                    color: Theme.error
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Q.Button {
                        id: cancelRenameButton
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        KeyNavigation.left: confirmRenameButton
                        KeyNavigation.right: confirmRenameButton
                        KeyNavigation.tab: confirmRenameButton
                        KeyNavigation.backtab: confirmRenameButton
                        Keys.onLeftPressed: confirmRenameButton.forceActiveFocus()
                        Keys.onRightPressed: confirmRenameButton.forceActiveFocus()
                        Keys.onEscapePressed: renameDialog.reject()
                        onClicked: renameDialog.reject()
                    }

                    Q.Button {
                        id: confirmRenameButton
                        text: "Rename"
                        variant: "primary"
                        size: "small"
                        KeyNavigation.left: cancelRenameButton
                        KeyNavigation.right: cancelRenameButton
                        KeyNavigation.tab: cancelRenameButton
                        KeyNavigation.backtab: cancelRenameButton
                        Keys.onLeftPressed: cancelRenameButton.forceActiveFocus()
                        Keys.onRightPressed: cancelRenameButton.forceActiveFocus()
                        Keys.onEscapePressed: renameDialog.reject()
                        onClicked: renameDialog.accept()
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
            newFolderErrorText.text = ""
            visible = true
            folderBox.opacity = 0
            folderBox.scale = 0.88
            folderBox.yOffset = -8
            folderOpenAnim.start()
            Qt.callLater(function() { newFolderField.inputItem.forceActiveFocus() })
            newFolderField.forceActiveFocus()
        }
        function accept() {
            var name = newFolderField.text.trim()
            if (newItemParentPath === "" || name === "") return
            var createdPath = newItemParentPath + "/" + name
            if (fileOps.pathExists(createdPath)) {
                newFolderErrorText.text = "\"" + name + "\" already exists"
                return
            }
            undoManager.createFolder(newItemParentPath, name)
            if (fileOps.pathExists(createdPath))
                root.focusPathInPane(root.activePane, createdPath, true)
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
            height: folderCard.implicitHeight
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: folderBox.yOffset }

            Q.Card {
                id: folderCard
                anchors.fill: parent
                title: "New Folder"
                padding: 20
                color: Theme.mantle
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)

                Q.TextField {
                    id: newFolderField
                    Layout.fillWidth: true
                    autoFocus: true
                    variant: "filled"
                    placeholder: "Folder name"
                    onTextChanged: newFolderErrorText.text = ""
                    Keys.onReturnPressed: newFolderDialog.accept()
                    Keys.onEscapePressed: newFolderDialog.reject()
                }

                Text {
                    id: newFolderErrorText
                    Layout.fillWidth: true
                    visible: text !== ""
                    color: Theme.error
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Q.Button {
                        id: cancelNewFolderButton
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        KeyNavigation.left: confirmNewFolderButton
                        KeyNavigation.right: confirmNewFolderButton
                        KeyNavigation.tab: confirmNewFolderButton
                        KeyNavigation.backtab: confirmNewFolderButton
                        Keys.onLeftPressed: confirmNewFolderButton.forceActiveFocus()
                        Keys.onRightPressed: confirmNewFolderButton.forceActiveFocus()
                        Keys.onEscapePressed: newFolderDialog.reject()
                        onClicked: newFolderDialog.reject()
                    }

                    Q.Button {
                        id: confirmNewFolderButton
                        text: "Create"
                        variant: "primary"
                        size: "small"
                        KeyNavigation.left: cancelNewFolderButton
                        KeyNavigation.right: cancelNewFolderButton
                        KeyNavigation.tab: cancelNewFolderButton
                        KeyNavigation.backtab: cancelNewFolderButton
                        Keys.onLeftPressed: cancelNewFolderButton.forceActiveFocus()
                        Keys.onRightPressed: cancelNewFolderButton.forceActiveFocus()
                        Keys.onEscapePressed: newFolderDialog.reject()
                        onClicked: newFolderDialog.accept()
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
            newFileErrorText.text = ""
            visible = true
            fileBox.opacity = 0
            fileBox.scale = 0.88
            fileBox.yOffset = -8
            fileOpenAnim.start()
            Qt.callLater(function() { newFileField.inputItem.forceActiveFocus() })
            newFileField.forceActiveFocus()
        }
        function accept() {
            var name = newFileField.text.trim()
            if (newItemParentPath === "" || name === "") return
            var createdPath = newItemParentPath + "/" + name
            if (fileOps.pathExists(createdPath)) {
                newFileErrorText.text = "\"" + name + "\" already exists"
                return
            }
            undoManager.createFile(newItemParentPath, name)
            if (fileOps.pathExists(createdPath))
                root.focusPathInPane(root.activePane, createdPath, true)
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
            height: fileCard.implicitHeight
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: fileBox.yOffset }

            Q.Card {
                id: fileCard
                anchors.fill: parent
                title: "New File"
                color: Theme.mantle
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                padding: 20

                Q.TextField {
                    id: newFileField
                    Layout.fillWidth: true
                    autoFocus: true
                    variant: "filled"
                    placeholder: "File name"
                    onTextChanged: newFileErrorText.text = ""
                    Keys.onReturnPressed: newFileDialog.accept()
                    Keys.onEscapePressed: newFileDialog.reject()
                }

                Text {
                    id: newFileErrorText
                    Layout.fillWidth: true
                    visible: text !== ""
                    color: Theme.error
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Q.Button {
                        id: cancelNewFileButton
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        KeyNavigation.left: confirmNewFileButton
                        KeyNavigation.right: confirmNewFileButton
                        KeyNavigation.tab: confirmNewFileButton
                        KeyNavigation.backtab: confirmNewFileButton
                        Keys.onLeftPressed: confirmNewFileButton.forceActiveFocus()
                        Keys.onRightPressed: confirmNewFileButton.forceActiveFocus()
                        Keys.onEscapePressed: newFileDialog.reject()
                        onClicked: newFileDialog.reject()
                    }

                    Q.Button {
                        id: confirmNewFileButton
                        text: "Create"
                        variant: "primary"
                        size: "small"
                        KeyNavigation.left: cancelNewFileButton
                        KeyNavigation.right: cancelNewFileButton
                        KeyNavigation.tab: cancelNewFileButton
                        KeyNavigation.backtab: cancelNewFileButton
                        Keys.onLeftPressed: cancelNewFileButton.forceActiveFocus()
                        Keys.onRightPressed: cancelNewFileButton.forceActiveFocus()
                        Keys.onEscapePressed: newFileDialog.reject()
                        onClicked: newFileDialog.accept()
                    }
                }
            }
        }
    }

    // ── Properties dialog ──────────────────────────────────────────────────
    Item {
        id: propertiesDialog
        anchors.fill: parent
        visible: false
        z: 1000

        property var props: ({})
        property var apps: []
        property var fileModelRef: fsModel
        property int currentTab: 0  // 0=General, 1=Permissions, 2=Open With

        function showProperties(path) {
            fileModelRef = root.paneBaseModel(root.activePane) || fsModel
            props = fileModelRef.fileProperties(path)
            currentTab = 0
            propsTabs.currentIndex = 0
            if (!props.isDir && props.mimeType)
                apps = fileModelRef.availableApps(props.mimeType)
            else
                apps = []
            visible = true
            propsBox.opacity = 0
            propsBox.scale = 0.88
            propsBox.yOffset = -8
            propsOpenAnim.start()
        }
        function close() { propsCloseAnim.start() }

        ParallelAnimation {
            id: propsOpenAnim
            NumberAnimation {
                target: propsBox; property: "opacity"
                from: 0; to: 1; duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: propsBox; property: "scale"
                from: 0.88; to: 1; duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 0.8
            }
            NumberAnimation {
                target: propsBox; property: "yOffset"
                from: -8; to: 0; duration: 220
                easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            id: propsCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: propsBox; property: "opacity"
                    to: 0; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: propsBox; property: "scale"
                    to: 0.92; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: propsBox; property: "yOffset"
                    to: -4; duration: 120
                    easing.type: Easing.InCubic
                }
            }
            ScriptAction { script: propertiesDialog.visible = false }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: propertiesDialog.close()
        }

        Item {
            id: propsBox
            width: 420
            height: propsOuterCol.height
            anchors.centerIn: parent
            opacity: 0; scale: 0.88; transformOrigin: Item.Center
            property real yOffset: 0
            transform: Translate { y: propsBox.yOffset }

            // Access dropdown options
            property var accessOptions: ["None", "Read only", "Read & Write", "Read, Write & Execute"]

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                radius: Theme.radiusMedium
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                border.width: 1
            }

            Column {
                id: propsOuterCol
                width: parent.width
                spacing: 0

                // ── Hero: icon + name + kind + size ──
                Item {
                    width: parent.width; height: 88
                    Rectangle {
                        id: propsIconBg; width: 52; height: 52; radius: 12
                        color: Theme.surface
                        anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter
                        Image {
                            anchors.centerIn: parent; width: 32; height: 32
                            source: propertiesDialog.props.iconName ? ("image://icon/" + propertiesDialog.props.iconName) : ""
                            sourceSize: Qt.size(32, 32); smooth: true
                        }
                    }
                    Column {
                        anchors.left: propsIconBg.right; anchors.leftMargin: 14
                        anchors.right: parent.right; anchors.rightMargin: 24
                        anchors.verticalCenter: parent.verticalCenter; spacing: 2
                        Text {
                            text: propertiesDialog.props.name || ""; color: Theme.text
                            font.pixelSize: 15; font.weight: Font.DemiBold
                            elide: Text.ElideMiddle; width: parent.width
                        }
                        Text {
                            text: { var p = propertiesDialog.props; return !p.mimeDescription ? "" : p.isDir ? "Folder" : p.mimeDescription }
                            color: Theme.subtext; font.pointSize: Theme.fontSmall; elide: Text.ElideRight; width: parent.width
                        }
                    }
                }

                // ── Tab bar ──
                Q.Tabs {
                    id: propsTabs
                    width: parent.width
                    model: propertiesDialog.props.canEditPermissions === false ? ["General"] : ["General", "Permissions"]
                    currentIndex: propertiesDialog.currentTab
                    onTabChanged: (index) => propertiesDialog.currentTab = index
                }

                // ── Tab content slider ──
                Item {
                    id: tabSlider
                    width: parent.width
                    height: propertiesDialog.currentTab === 0 ? generalTab.height : permissionsTab.height
                    clip: true
                    Behavior on height { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

                    Row {
                        id: tabSliderRow
                        x: -propertiesDialog.currentTab * tabSlider.width
                        Behavior on x { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

                // ══════════════════════════════════════════════
                // TAB 0: General
                // ══════════════════════════════════════════════
                Column {
                    id: generalTab
                    width: tabSlider.width; spacing: 0

                    // helper component for a property row
                    component PropRow: Item {
                        property string label
                        property string value
                        property bool show: true
                        width: parent.width; height: show ? 28 : 0; visible: show
                        Text { text: label; color: Theme.subtext; font.pointSize: Theme.fontSmall; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: 80 }
                        Text { text: value; color: Theme.text; font.pointSize: Theme.fontSmall; anchors.left: parent.left; anchors.leftMargin: 88; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideMiddle }
                    }

                    Item { width: 1; height: 8 }

                    // Info rows
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 0

                        PropRow { label: "Kind"; value: { var p = propertiesDialog.props; return p.isDir ? "Folder" : (p.mimeDescription || "") } }
                        PropRow { label: "Location"; value: propertiesDialog.props.parentDir || "" }
                        PropRow { label: "Deleted"; value: propertiesDialog.props.deleted || ""; show: (propertiesDialog.props.deleted || "") !== "" }
                        PropRow { label: "Link target"; value: propertiesDialog.props.symlinkTarget || ""; show: propertiesDialog.props.isSymlink || false }
                    }

                    // Separator
                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    // Timestamps
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 0

                        PropRow { label: "Created"; value: propertiesDialog.props.created || "" }
                        PropRow { label: "Modified"; value: propertiesDialog.props.modified || "" }
                        PropRow { label: "Accessed"; value: propertiesDialog.props.accessed || "" }
                    }

                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    // Size section
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 0

                        PropRow { label: "Size"; value: propertiesDialog.props.sizeText || "" }
                        PropRow { label: "Content"; value: propertiesDialog.props.contentText || ""; show: propertiesDialog.props.isDir || false }
                    }

                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    // Disk usage
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 4
                        visible: propertiesDialog.props.diskTotal !== undefined

                        Item { width: 1; height: 4 }

                        PropRow { label: "Capacity"; value: propertiesDialog.props.diskTotal || "" }

                        // Usage bar
                        Item {
                            width: parent.width; height: 28
                            Text { text: "Usage"; color: Theme.subtext; font.pointSize: Theme.fontSmall; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: 80 }
                            Column {
                                anchors.left: parent.left; anchors.leftMargin: 88
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 4

                                // Bar
                                Rectangle {
                                    width: parent.width; height: 6; radius: 3
                                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                                    Rectangle {
                                        width: parent.width * (propertiesDialog.props.diskUsedPercent || 0)
                                        height: parent.height; radius: 3
                                        color: (propertiesDialog.props.diskUsedPercent || 0) > 0.9 ? "#e74c3c" : Theme.accent
                                    }
                                }

                                // Label
                                Text {
                                    text: (propertiesDialog.props.diskUsed || "") + " used (" + (propertiesDialog.props.diskUsedPctText || "") + ")  |  " +
                                          (propertiesDialog.props.diskFree || "") + " free (" + (propertiesDialog.props.diskFreePctText || "") + ")"
                                    color: Theme.subtext; font.pixelSize: 10
                                }
                            }
                        }

                        Item { width: 1; height: 4 }
                    }

                    // Open With (files only)
                    Q.Collapsible {
                        visible: !(propertiesDialog.props.isDir) && propertiesDialog.apps.length > 0
                        title: {
                            var apps = propertiesDialog.apps
                            for (var i = 0; i < apps.length; i++)
                                if (apps[i].isDefault) return "Open with: " + apps[i].name
                            return apps.length > 0 ? "Open with: " + apps[0].name : "Open with"
                        }
                        width: parent.width

                        Repeater {
                            model: propertiesDialog.apps
                            delegate: Rectangle {
                                width: parent ? parent.width : 0; height: 30; radius: 4
                                color: owItemMa.containsMouse
                                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.1)
                                    : "transparent"
                                Layout.fillWidth: true

                                Image {
                                    id: owAppIcon
                                    source: modelData.iconName ? ("image://icon/" + modelData.iconName) : ""
                                    sourceSize: Qt.size(18, 18)
                                    width: 18; height: 18
                                    anchors.left: parent.left; anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: modelData.iconName && status === Image.Ready
                                }

                                Text {
                                    text: modelData.name
                                    color: modelData.isDefault ? Theme.accent : Theme.text
                                    font.pointSize: Theme.fontSmall
                                    font.weight: modelData.isDefault ? Font.DemiBold : Font.Normal
                                    anchors.left: owAppIcon.visible ? owAppIcon.right : parent.left
                                    anchors.leftMargin: owAppIcon.visible ? 8 : 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.right: owItemBadge.left; anchors.rightMargin: 4
                                    elide: Text.ElideRight
                                }

                                IconCheck {
                                    id: owItemBadge
                                    visible: modelData.isDefault
                                    size: 14; color: Theme.accent
                                    anchors.right: parent.right; anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                MouseArea {
                                    id: owItemMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!modelData.isDefault) {
                                            propertiesDialog.fileModelRef.setDefaultApp(propertiesDialog.props.mimeType, modelData.desktopFile)
                                            propertiesDialog.apps = propertiesDialog.fileModelRef.availableApps(propertiesDialog.props.mimeType)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ══════════════════════════════════════════════
                // TAB 1: Permissions
                // ══════════════════════════════════════════════
                Column {
                    id: permissionsTab
                    width: tabSlider.width; spacing: 0

                    Item { width: 1; height: 12 }

                    // Helper component for permission group
                    component PermGroup: Column {
                        property string groupLabel
                        property string userName
                        property int accessIdx: 0
                        signal accessChanged(int newIdx)
                        width: parent.width; spacing: 4

                        // Group header
                        Text {
                            text: groupLabel
                            color: Theme.text; font.pointSize: Theme.fontSmall; font.weight: Font.DemiBold
                            leftPadding: 24
                        }

                        // User name (if any)
                        Text {
                            text: userName; visible: userName !== ""
                            color: Theme.subtext; font.pointSize: Theme.fontSmall
                            leftPadding: 36
                        }

                        // Access selector row
                        Item {
                            width: parent.width; height: 34
                            Text {
                                text: "Access"
                                color: Theme.subtext; font.pointSize: Theme.fontSmall
                                anchors.left: parent.left; anchors.leftMargin: 36
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Q.Dropdown {
                                model: propsBox.accessOptions
                                currentIndex: accessIdx
                                label: ""
                                anchors.left: parent.left; anchors.leftMargin: 100
                                anchors.right: parent.right; anchors.rightMargin: 24
                                onSelected: (index, value) => accessChanged(index)
                            }
                        }

                        Item { width: 1; height: 4 }
                    }

                    PermGroup {
                        groupLabel: "Owner"
                        userName: propertiesDialog.props.owner || ""
                        accessIdx: propertiesDialog.props.ownerAccess || 0
                        onAccessChanged: (idx) => {
                            propertiesDialog.fileModelRef.setFilePermissions(propertiesDialog.props.path, idx, propertiesDialog.props.groupAccess || 0, propertiesDialog.props.otherAccess || 0)
                            propertiesDialog.props = propertiesDialog.fileModelRef.fileProperties(propertiesDialog.props.path)
                        }
                    }

                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    PermGroup {
                        groupLabel: "Group"
                        userName: propertiesDialog.props.group || ""
                        accessIdx: propertiesDialog.props.groupAccess || 0
                        onAccessChanged: (idx) => {
                            propertiesDialog.fileModelRef.setFilePermissions(propertiesDialog.props.path, propertiesDialog.props.ownerAccess || 0, idx, propertiesDialog.props.otherAccess || 0)
                            propertiesDialog.props = propertiesDialog.fileModelRef.fileProperties(propertiesDialog.props.path)
                        }
                    }

                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    PermGroup {
                        groupLabel: "Others"
                        userName: ""
                        accessIdx: propertiesDialog.props.otherAccess || 0
                        onAccessChanged: (idx) => {
                            propertiesDialog.fileModelRef.setFilePermissions(propertiesDialog.props.path, propertiesDialog.props.ownerAccess || 0, propertiesDialog.props.groupAccess || 0, idx)
                            propertiesDialog.props = propertiesDialog.fileModelRef.fileProperties(propertiesDialog.props.path)
                        }
                    }

                    Item { width: 1; height: 8 }
                }

                    } // Row (tabSliderRow)
                } // Item (tabSlider)

                // ── Close button ──
                Item {
                    width: parent.width; height: 48
                    Q.Button {
                        text: "Close"
                        variant: "primary"
                        size: "small"
                        anchors.right: parent.right; anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: propertiesDialog.close()
                    }
                }
            }
        }
    }

    Q.Dialog {
        id: conflictDialog
        anchors.fill: parent
        z: 9998
        dialogWidth: 460
        title: root.transferMoveOperation ? "Move Conflict" : "Copy Conflict"
        initialFocusItem: conflictRenameField

        property var currentItem: ({})

        onRejected: {
            root.resetTransferConflictState()
            root.scheduleActivePaneFocus()
        }

        Text {
            Layout.fillWidth: true
            text: conflictDialog.currentItem.samePath
                ? "\"" + (conflictDialog.currentItem.sourceName || "") + "\" is already in this folder."
                : "\"" + (conflictDialog.currentItem.sourceName || "") + "\" already exists in the destination."
            color: Theme.text
            font.pointSize: Theme.fontNormal
            wrapMode: Text.WordWrap
        }

        Text {
            Layout.fillWidth: true
            text: root.transferMoveOperation
                ? "Choose whether to skip it, replace the existing item, or keep both with a new name."
                : "Choose whether to skip it, overwrite the existing item, or keep both with a new name."
            color: Theme.subtext
            font.pointSize: Theme.fontNormal
            wrapMode: Text.WordWrap
        }

        Q.TextField {
            id: conflictRenameField
            Layout.fillWidth: true
            autoFocus: true
            variant: "filled"
            placeholder: "New name"
            Keys.onReturnPressed: root.resolveTransferConflict("rename")
            Keys.onEscapePressed: conflictDialog.reject()
        }

        Text {
            id: conflictErrorText
            Layout.fillWidth: true
            visible: text !== ""
            color: Theme.error
            font.pointSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 12

            Q.Button {
                id: cancelConflictButton
                text: "Cancel"
                variant: "ghost"
                size: "small"
                onClicked: conflictDialog.reject()
            }

            Q.Button {
                id: skipConflictButton
                text: "Skip"
                variant: "ghost"
                size: "small"
                onClicked: root.resolveTransferConflict("skip")
            }

            Q.Button {
                id: overwriteConflictButton
                text: root.transferMoveOperation ? "Replace" : "Overwrite"
                variant: "danger"
                size: "small"
                enabled: !(conflictDialog.currentItem.samePath || false)
                onClicked: root.resolveTransferConflict("overwrite")
            }

            Q.Button {
                id: renameConflictButton
                text: "Rename"
                variant: "primary"
                size: "small"
                onClicked: root.resolveTransferConflict("rename")
            }
        }
    }

    // ── Permanent Delete Confirmation Dialog ───────────────────────────────
    Q.Dialog {
        id: deleteConfirmDialog
        anchors.fill: parent
        z: 9998
        dialogWidth: 360
        title: "Permanently Delete?"
        initialFocusItem: cancelDeleteButton
        onAccepted: fileOps.deleteFiles(root.deleteConfirmPaths)

        Text {
            Layout.fillWidth: true
            text: root.deleteConfirmPaths.length === 1
                ? "\"" + root.deleteConfirmPaths[0].substring(root.deleteConfirmPaths[0].lastIndexOf("/") + 1) + "\" will be permanently deleted. This cannot be undone."
                : root.deleteConfirmPaths.length + " items will be permanently deleted. This cannot be undone."
            color: Theme.subtext
            font.pointSize: Theme.fontNormal
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 12

            Q.Button {
                id: cancelDeleteButton
                text: "Cancel"
                variant: "ghost"
                size: "small"
                KeyNavigation.left: confirmDeleteButton
                KeyNavigation.right: confirmDeleteButton
                KeyNavigation.tab: confirmDeleteButton
                KeyNavigation.backtab: confirmDeleteButton
                Keys.onLeftPressed: confirmDeleteButton.forceActiveFocus()
                Keys.onRightPressed: confirmDeleteButton.forceActiveFocus()
                onClicked: deleteConfirmDialog.reject()
            }

            Q.Button {
                id: confirmDeleteButton
                text: "Delete"
                variant: "danger"
                size: "small"
                KeyNavigation.left: cancelDeleteButton
                KeyNavigation.right: cancelDeleteButton
                KeyNavigation.tab: cancelDeleteButton
                KeyNavigation.backtab: cancelDeleteButton
                Keys.onLeftPressed: cancelDeleteButton.forceActiveFocus()
                Keys.onRightPressed: cancelDeleteButton.forceActiveFocus()
                onClicked: deleteConfirmDialog.accept()
            }
        }
    }

    // ── Empty Trash Confirmation Dialog ──────────────────────────────────────
    Q.Dialog {
        id: emptyTrashConfirmDialog
        anchors.fill: parent
        z: 9998
        dialogWidth: 360
        title: "Empty Trash?"
        initialFocusItem: cancelEmptyTrashButton
        onAccepted: fileOps.emptyTrash()

        Text {
            Layout.fillWidth: true
            text: "All items in the Trash will be permanently deleted. This cannot be undone."
            color: Theme.subtext
            font.pointSize: Theme.fontNormal
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 12

            Q.Button {
                id: cancelEmptyTrashButton
                text: "Cancel"
                variant: "ghost"
                size: "small"
                KeyNavigation.left: confirmEmptyTrashButton
                KeyNavigation.right: confirmEmptyTrashButton
                KeyNavigation.tab: confirmEmptyTrashButton
                KeyNavigation.backtab: confirmEmptyTrashButton
                Keys.onLeftPressed: confirmEmptyTrashButton.forceActiveFocus()
                Keys.onRightPressed: confirmEmptyTrashButton.forceActiveFocus()
                onClicked: emptyTrashConfirmDialog.reject()
            }

            Q.Button {
                id: confirmEmptyTrashButton
                text: "Empty Trash"
                variant: "danger"
                size: "small"
                KeyNavigation.left: cancelEmptyTrashButton
                KeyNavigation.right: cancelEmptyTrashButton
                KeyNavigation.tab: cancelEmptyTrashButton
                KeyNavigation.backtab: cancelEmptyTrashButton
                Keys.onLeftPressed: cancelEmptyTrashButton.forceActiveFocus()
                Keys.onRightPressed: cancelEmptyTrashButton.forceActiveFocus()
                onClicked: emptyTrashConfirmDialog.accept()
            }
        }
    }

    // ── Context Menu ────────────────────────────────────────────────────────
    ContextMenu {
        id: contextMenu
        blurSource: mainContent

        fileModel: root.paneBaseModel(root.activePane)
        splitViewEnabled: root.splitViewEnabled()
        isTrashView: root.isTrashView
        currentViewMode: tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
        currentSortBy: tabModel.activeTab ? tabModel.activeTab.sortBy : "name"
        currentSortAscending: tabModel.activeTab ? tabModel.activeTab.sortAscending : true

        onOpenRequested: (path) => fileOps.openFile(path)
        onOpenWithRequested: (path, desktopFile) => fileOps.openFileWith(path, desktopFile)

        onCutRequested: (paths) => clipboard.cut(paths)

        onCopyRequested: (paths) => clipboard.copy(paths)

        onPasteRequested: (destPath) => {
            root.pasteIntoDirectory(destPath)
        }

        onCopyPathRequested: (path) => fileOps.copyPathToClipboard(path)

        onRenameRequested: (path) => {
            root.renameTargetPath = path
            var name = path.substring(path.lastIndexOf("/") + 1)
            renameField.text = name
            renameDialog.open()
        }

        onTrashRequested: (paths) => undoManager.trashFiles(paths)
        onRestoreRequested: (paths) => fileOps.restoreFromTrash(paths)
        onEmptyTrashRequested: emptyTrashConfirmDialog.open()

        onDeleteRequested: (paths) => {
            deleteConfirmPaths = paths
            deleteConfirmDialog.open()
        }

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

        onSelectAllRequested: {
            var view = root.activeFileView()
            if (view) view.selectAll()
        }

        onPropertiesRequested: (path) => {
            propertiesDialog.showProperties(path)
        }

        onSplitViewRequested: (path) => {
            root.openPathInSplitView(path)
        }

        onViewModeRequested: (mode) => {
            if (tabModel.activeTab) tabModel.activeTab.viewMode = mode
        }

        onSortRequested: (column, ascending) => {
            if (tabModel.activeTab) {
                tabModel.activeTab.sortBy = column
                tabModel.activeTab.sortAscending = ascending
            }
            if (fsModel) fsModel.sortByColumn(column, ascending)
            if (splitFsModel) splitFsModel.sortByColumn(column, ascending)
        }
    }

    ContextMenu {
        id: sidebarContextMenu
        menuWidth: 220

        property var sidebarItem: ({})

        onOpenRequested: (path) => {
            if (sidebarItem.isRecents) {
                root.setPaneRecents(root.activePane, true)
                return
            }

            root.navigateActivePaneTo(path)
        }

        onPropertiesRequested: (path) => {
            if (path)
                propertiesDialog.showProperties(path)
        }

        onOpenInTerminalRequested: (path) => {
            if (path)
                fileOps.openInTerminal(path)
        }

        onCustomActionRequested: (action) => {
            if (action === "opennewtab") {
                root.setPaneRecents(root.activePane, false)
                tabModel.addTab()
                if (tabModel.activeTab)
                    tabModel.activeTab.navigateTo(sidebarContextMenu.targetPath)
            } else if (action === "emptytrash") {
                emptyTrashConfirmDialog.open()
            } else if (action === "removebookmark") {
                if (sidebarItem.kind === "bookmark" && sidebarItem.index >= 0)
                    bookmarks.removeBookmark(sidebarItem.index)
            } else if (action === "mountdevice") {
                if (sidebarItem.kind === "device" && sidebarItem.index >= 0)
                    devices.mount(sidebarItem.index)
            } else if (action === "unmountdevice") {
                if (sidebarItem.kind === "device" && sidebarItem.index >= 0)
                    devices.unmount(sidebarItem.index)
            }
        }

        onVisibleChanged: {
            if (!visible)
                sidebarItem = ({})
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
        onActivated: root.goActivePaneBack()
    }

    Shortcut {
        sequence: "Backspace"
        onActivated: root.goActivePaneBack()
    }

    Shortcut {
        sequence: config.shortcut("forward")
        onActivated: root.goActivePaneForward()
    }

    Shortcut {
        sequence: config.shortcut("parent")
        onActivated: root.goActivePaneUp()
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

    Shortcut {
        sequence: config.shortcut("split_view")
        onActivated: root.toggleSplitView()
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
            if (!clipboard.hasContent && !fileOps.hasClipboardImage()) return
            if (root.paneIsRecents(activePane)) return
            var dest = panePath(activePane)
            if (dest === "") return
            root.pasteIntoDirectory(dest)
        }
    }

    Shortcut {
        sequence: config.shortcut("trash")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length === 0) return
            if (root.isTrashView) {
                deleteConfirmPaths = paths
                deleteConfirmDialog.open()
            } else {
                undoManager.trashFiles(paths)
            }
        }
    }

    Shortcut {
        sequence: config.shortcut("permanent_delete")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length > 0) {
                deleteConfirmPaths = paths
                deleteConfirmDialog.open()
            }
        }
    }

    Shortcut {
        sequence: config.shortcut("undo")
        onActivated: { if (undoManager.canUndo) undoManager.undo() }
    }

    Shortcut {
        sequence: config.shortcut("redo")
        onActivated: { if (undoManager.canRedo) undoManager.redo() }
    }

    Shortcut {
        sequence: config.shortcut("select_all")
        onActivated: {
            var view = root.activeFileView()
            if (view) view.selectAll()
        }
    }

    Shortcut {
        sequence: config.shortcut("rename")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length === 1) {
                root.renameTargetPath = paths[0]
                var name = paths[0].substring(paths[0].lastIndexOf("/") + 1)
                renameField.text = name
                renameDialog.open()
            }
        }
    }

    Shortcut {
        sequence: config.shortcut("new_folder")
        onActivated: {
            var dest = root.isRecentsView ? "" : panePath(activePane)
            if (dest !== "") {
                root.newItemParentPath = dest
                newFolderField.text = ""
                newFolderDialog.open()
            }
        }
    }

    Shortcut {
        sequence: config.shortcut("new_file")
        onActivated: {
            var dest = root.isRecentsView ? "" : panePath(activePane)
            if (dest !== "") {
                root.newItemParentPath = dest
                newFileField.text = ""
                newFileDialog.open()
            }
        }
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

    // Search
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            if (root.searchMode) root.closeSearch()
            else root.openSearch()
        }
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.searchMode
                 && !quickPreview.active
                 && !renameDialog.visible
                 && !newFolderDialog.visible
                 && !newFileDialog.visible
                 && !deleteConfirmDialog.visible
                 && !emptyTrashConfirmDialog.visible
        onActivated: root.closeSearch()
    }

    function sidebarMenuItems(item) {
        if (!item)
            return []

        if (item.kind === "quickAccess") {
            if (item.isRecents)
                return [
                    { text: "Open", shortcut: "", action: "open" }
                ]

            if (fileOps.isTrashPath(item.path))
                return [
                    { text: "Open", shortcut: "Return", action: "open" },
                    { text: "Open in New Tab", shortcut: "", action: "opennewtab" },
                    { separator: true },
                    { text: "Empty Trash", shortcut: "", action: "emptytrash", destructive: true }
                ]

            return [
                { text: "Open", shortcut: "Return", action: "open" },
                { text: "Open in New Tab", shortcut: "", action: "opennewtab" },
                { separator: true },
                { text: "Open in Terminal", shortcut: "", action: "terminal" },
                { text: "Properties", shortcut: "", action: "properties" }
            ]
        }

        if (item.kind === "bookmark") {
            return [
                { text: "Open", shortcut: "Return", action: "open" },
                { text: "Open in New Tab", shortcut: "", action: "opennewtab" },
                { separator: true },
                { text: "Open in Terminal", shortcut: "", action: "terminal" },
                { text: "Properties", shortcut: "", action: "properties" },
                { separator: true },
                { text: "Remove from Bookmarks", shortcut: "", action: "removebookmark", destructive: true }
            ]
        }

        if (item.kind === "device") {
            if (!item.mounted)
                return [
                    { text: "Mount", shortcut: "", action: "mountdevice" }
                ]

            return [
                { text: "Open", shortcut: "Return", action: "open" },
                { text: "Open in New Tab", shortcut: "", action: "opennewtab" },
                { separator: true },
                { text: "Open in Terminal", shortcut: "", action: "terminal" },
                { text: "Properties", shortcut: "", action: "properties" },
                { separator: true },
                { text: "Unmount", shortcut: "", action: "unmountdevice" }
            ]
        }

        return []
    }

    function handlePaneFileActivated(pane, filePath, isDirectory) {
        root.setActivePane(pane)

        if (isDirectory) {
            root.navigatePaneTo(pane, filePath)
        } else if (fileOps.isArchive(filePath)) {
            var dir = filePath.substring(0, filePath.lastIndexOf("/"))
            var rootFolder = fileOps.archiveRootFolder(filePath)
            fileOps.extractArchive(filePath, dir)
            var conn = fileOps.operationFinished.connect(function(success) {
                fileOps.operationFinished.disconnect(arguments.callee)
                if (success) {
                    root.navigatePaneTo(pane, rootFolder ? dir + "/" + rootFolder : dir)
                }
            })
        } else {
            fileOps.openFile(filePath)
            recentFiles.addRecent(filePath)
        }
    }

    function showContextMenuForPane(pane, filePath, isDirectory, position) {
        root.setActivePane(pane)

        var currentDir = panePath(pane)
        contextMenu.targetPath = filePath !== "" ? filePath : currentDir
        contextMenu.targetIsDir = filePath !== "" ? isDirectory : true
        contextMenu.isEmptySpace = (filePath === "")
        var sel = getSelectedPaths(pane)
        contextMenu.selectedPaths = (sel.length > 1) ? sel : (filePath !== "" ? [filePath] : [])
        contextMenu.popup(position.x, position.y)
    }

    function openPathInSplitView(path) {
        if (!tabModel.activeTab)
            return

        var targetPath = path || panePath(activePane)
        if (!targetPath)
            return

        root.clearPaneSearch("secondary")
        root.setPaneRecents("secondary", false)
        tabModel.activeTab.resetSecondaryTo(targetPath)

        if (!tabModel.activeTab.splitViewEnabled)
            tabModel.activeTab.splitViewEnabled = true

        root.setActivePane("secondary")
    }

    function pasteIntoDirectory(destPath) {
        if (!destPath)
            return

        if (clipboard.hasContent) {
            var wasCut = clipboard.isCut
            var items = clipboard.paths
            if (!items || items.length === 0) return
            beginTransfer(items, destPath, wasCut, wasCut)
            return
        }

        if (fileOps.hasClipboardImage())
            fileOps.pasteClipboardImage(destPath)
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    RowLayout {
        id: mainContent
        anchors.fill: parent
        spacing: 0

        // Sidebar (full height, animated)
        Item {
            Layout.preferredWidth: root.sidebarVisible ? config.sidebarWidth : 0
            Layout.fillHeight: true
            clip: true

            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 200; easing.type: Easing.InOutCubic }
            }

            Sidebar {
                width: config.sidebarWidth
                height: parent.height
                currentPath: panePath(activePane)
                trashPath: root.unifiedTrashPath
                isRecentsView: root.isRecentsView
                onBookmarkClicked: (path) => {
                    root.navigateActivePaneTo(path)
                }
                onSidebarContextMenuRequested: (item, position) => {
                    sidebarContextMenu.sidebarItem = item
                    sidebarContextMenu.contextData = item
                    sidebarContextMenu.customItems = root.sidebarMenuItems(item)
                    sidebarContextMenu.targetPath = item.path || ""
                    sidebarContextMenu.targetIsDir = !!item.path
                    sidebarContextMenu.isEmptySpace = false
                    sidebarContextMenu.selectedPaths = item.path ? [item.path] : []
                    sidebarContextMenu.popup(position.x, position.y)
                }
                onRecentsClicked: {
                    root.setPaneRecents(root.activePane, true)
                }
                onCollapseClicked: root.sidebarVisible = !root.sidebarVisible
            }
        }

        // Right panel: toolbar + content
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

            // Toolbar with integrated tabs
            Toolbar {
                id: toolbar
                Layout.fillWidth: true
                activeTab: tabModel.activeTab
                navigationPath: panePath(activePane)
                canGoBack: activePaneCanGoBack()
                canGoForward: activePaneCanGoForward()
                splitViewEnabled: root.splitViewEnabled()
                isRecentsView: root.isRecentsView
                isTrashView: root.isTrashView
                searchMode: root.searchMode
                currentSearchQuery: root.searchProxyForPane(activePane).searchQuery
                searchTypeFilter: root.searchProxyForPane(activePane).fileTypeFilter
                searchDateFilter: root.searchProxyForPane(activePane).dateFilter
                searchSizeFilter: root.searchProxyForPane(activePane).sizeFilter
                filterPanelOpen: root.paneFilterPanelOpen(activePane)
                onBackRequested: root.goActivePaneBack()
                onForwardRequested: root.goActivePaneForward()
                onUpRequested: root.goActivePaneUp()
                onNavigateRequested: (targetPath) => root.navigateActivePaneTo(targetPath)
                onRestoreTrashRequested: {
                    var paths = getSelectedPaths()
                    if (paths.length > 0)
                        fileOps.restoreFromTrash(paths)
                }
                onEmptyTrashRequested: emptyTrashConfirmDialog.open()
                onSplitViewToggled: root.toggleSplitView()
                onHomeClicked: {
                    root.navigateActivePaneTo(fsModel.homePath())
                }
                onSearchClicked: root.openSearch()
                onSearchClosed: root.closeSearch()
                onSearchQueryChanged: (query) => root.handleSearchQuery(query)
                onSearchEnterPressed: root.handleSearchEnter()
                onSearchNavigateDown: {
                    var subView = root.activeSubView()
                    if (subView) subView.forceActiveFocus()
                }
                onSearchFilterToggled: root.setPaneFilterPanelOpen(activePane, !root.paneFilterPanelOpen(activePane))
                onTypeFilterChanged: (filter) => root.searchProxyForPane(activePane).fileTypeFilter = filter
                onDateFilterChanged: (filter) => root.searchProxyForPane(activePane).dateFilter = filter
                onSizeFilterChanged: (filter) => root.searchProxyForPane(activePane).sizeFilter = filter
                onClearAllFilters: {
                    root.searchProxyForPane(activePane).fileTypeFilter = ""
                    root.searchProxyForPane(activePane).dateFilter = ""
                    root.searchProxyForPane(activePane).sizeFilter = ""
                }
            }

            // File view (semi-transparent — Hyprland compositor blurs behind this)
            Rectangle {
                id: contentArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.65)

                // Curved mantle fills for inverse rounded corners
                Shape {
                    z: 1; width: Theme.radiusMedium; height: Theme.radiusMedium
                    anchors.top: parent.top; anchors.left: parent.left
                    ShapePath {
                        fillColor: Theme.mantle; strokeColor: "transparent"
                        startX: 0; startY: 0
                        PathLine { x: Theme.radiusMedium; y: 0 }
                        PathArc {
                            x: 0; y: Theme.radiusMedium
                            radiusX: Theme.radiusMedium; radiusY: Theme.radiusMedium
                            direction: PathArc.Counterclockwise
                        }
                        PathLine { x: 0; y: 0 }
                    }
                }
                Shape {
                    z: 1; width: Theme.radiusMedium; height: Theme.radiusMedium
                    anchors.top: parent.top; anchors.right: parent.right
                    ShapePath {
                        fillColor: Theme.mantle; strokeColor: "transparent"
                        startX: Theme.radiusMedium; startY: 0
                        PathLine { x: 0; y: 0 }
                        PathArc {
                            x: Theme.radiusMedium; y: Theme.radiusMedium
                            radiusX: Theme.radiusMedium; radiusY: Theme.radiusMedium
                            direction: PathArc.Clockwise
                        }
                        PathLine { x: Theme.radiusMedium; y: 0 }
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Rectangle {
                        id: primaryPaneFrame
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radiusMedium
                        clip: true
                        color: Qt.rgba(Theme.crust.r, Theme.crust.g, Theme.crust.b, 0.14)
                        border.width: 1
                        border.color: root.splitViewEnabled() && root.activePane === "primary"
                            ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
                            : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                        Behavior on border.color { ColorAnimation { duration: Theme.animDuration } }

                        FileViewContainer {
                            id: primaryFileViewContainer
                            anchors.fill: parent
                            fileModel: root.paneModel("primary")
                            viewMode: tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
                            currentPath: root.panePath("primary")

                            onInteractionStarted: root.setActivePane("primary")
                            onFileActivated: (filePath, isDirectory) => root.handlePaneFileActivated("primary", filePath, isDirectory)
                            onSelectionChanged: {
                                root.setActivePane("primary")
                                root.updateSelectionStatus()
                            }
                            onTransferRequested: (paths, destinationPath, moveOperation) => {
                                root.setActivePane("primary")
                                root.beginTransfer(paths, destinationPath, moveOperation, false)
                            }
                            onContextMenuRequested: (filePath, isDirectory, position) =>
                                root.showContextMenuForPane("primary", filePath, isDirectory, position)
                        }
                    }

                    Loader {
                        id: dividerLoader
                        active: root.splitViewEnabled()
                        visible: active
                        Layout.preferredWidth: active ? 1 : 0
                        Layout.fillHeight: true

                        sourceComponent: Rectangle {
                            width: 1
                            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                        }
                    }

                    Loader {
                        id: secondaryPaneLoader
                        active: root.splitViewEnabled()
                        visible: active
                        Layout.fillWidth: active
                        Layout.fillHeight: true

                        sourceComponent: Rectangle {
                            id: secondaryPaneFrame
                            property alias fileView: secondaryFileViewContainer
                            radius: Theme.radiusMedium
                            clip: true
                            color: Qt.rgba(Theme.crust.r, Theme.crust.g, Theme.crust.b, 0.14)
                            border.width: 1
                            border.color: root.activePane === "secondary"
                                ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.45)
                                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                            Behavior on border.color { ColorAnimation { duration: Theme.animDuration } }

                            FileViewContainer {
                                id: secondaryFileViewContainer
                                anchors.fill: parent
                                fileModel: root.paneModel("secondary")
                                viewMode: tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
                                currentPath: root.panePath("secondary")

                                onInteractionStarted: root.setActivePane("secondary")
                                onFileActivated: (filePath, isDirectory) => root.handlePaneFileActivated("secondary", filePath, isDirectory)
                                onSelectionChanged: {
                                    root.setActivePane("secondary")
                                    root.updateSelectionStatus()
                                }
                                onTransferRequested: (paths, destinationPath, moveOperation) => {
                                    root.setActivePane("secondary")
                                    root.beginTransfer(paths, destinationPath, moveOperation, false)
                                }
                                onContextMenuRequested: (filePath, isDirectory, position) =>
                                    root.showContextMenuForPane("secondary", filePath, isDirectory, position)
                            }
                        }
                    }
                }
            }

            StatusBar {
                Layout.fillWidth: true
                itemCount: root.activeItemCount()
                folderCount: root.activeFolderCount()
                searchStatus: root.searchMode && root.searchServiceForPane(activePane).isSearching
                    ? "Searching... " + root.searchServiceForPane(activePane).resultCount + " results"
                    : (root.searchMode && root.searchProxyForPane(activePane).searchActive
                        ? root.searchProxyForPane(activePane).rowCount() + " results"
                        : "")
                selectedCount: root.currentSelectedCount
                selectedSize: root.currentSelectedSize
            }
            }
        }
    }

    // ── Mouse back/forward button support ────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        z: -100
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        propagateComposedEvents: true
        onClicked: (mouse) => {
            if (mouse.button === Qt.BackButton)
                root.goActivePaneBack()
            else if (mouse.button === Qt.ForwardButton)
                root.goActivePaneForward()
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
            fsModel.refresh()
            splitFsModel.refresh()
            if (success)
                toast.show("Operation completed successfully", "success")
            else
                toast.show(error || "Operation failed", "error")
        }
    }
}
