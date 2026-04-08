import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import HyprFM

// Custom context menu — Hyprland compositor handles blur via windowrule
Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 9999
    Accessible.role: Accessible.PopupMenu
    Accessible.name: "Context menu"

    property string targetPath: ""
    property bool targetIsDir: false
    property bool isEmptySpace: false
    property var selectedPaths: []
    property var customItems: []
    property var contextData: ({})
    property int menuWidth: 260
    property bool splitViewEnabled: false
    property bool isTrashView: false

    signal openRequested(string path, bool isDir)
    signal openWithRequested(string path, string desktopFile)
    signal setDefaultAppRequested(string mimeType, string desktopFile)
    signal chooseAppRequested(string path, string mimeType)
    signal openInNewTabRequested(string path)
    signal cutRequested(var paths)
    signal copyRequested(var paths)
    signal pasteRequested(string destPath)
    signal copyPathRequested(string path)
    signal renameRequested(string path)
    signal bulkRenameRequested(var paths)
    signal trashRequested(var paths)
    signal restoreRequested(var paths)
    signal deleteRequested(var paths)
    signal openInTerminalRequested(string path)
    signal newFolderRequested(string parentPath)
    signal newFileRequested(string parentPath)
    signal selectAllRequested()
    signal propertiesRequested(string path)
    signal splitViewRequested(string path)
    signal viewModeRequested(string mode)
    signal sortRequested(string column, bool ascending)
    signal emptyTrashRequested()
    signal customActionRequested(string action)

    property string currentViewMode: "grid"
    property string currentSortBy: "name"
    property bool currentSortAscending: true
    readonly property bool hasCustomItems: customItems && customItems.length > 0
    readonly property bool remoteContext: fileOps.isRemotePath(targetPath) || fileOps.isRemotePath(effectiveDir)

    property var effectivePaths: (selectedPaths.length > 0) ? selectedPaths : (targetPath !== "" ? [targetPath] : [])
    property string effectiveDir: {
        if (isEmptySpace) return targetPath
        if (targetIsDir) return targetPath
        var p = targetPath
        var idx = p.lastIndexOf("/")
        return idx > 0 ? p.substring(0, idx) : "/"
    }

    property Item blurSource: null
    property var fileModel: null

    // Pending popup coordinates — repositioned after layout completes
    property real _pendingX: 0
    property real _pendingY: 0
    property bool _pendingPopup: false
    property var submenuItems: []
    property string activeSubmenuKey: ""
    property Item activeSubmenuSource: null
    property bool submenuVisible: false
    property bool _pendingSubmenuPopup: false
    property bool _animateSubmenuOnShow: false
    property real _submenuStartOffset: -10
    property real _submenuGap: 10
    property real _submenuPointerDepth: 8
    property real _submenuPointerBase: 12
    property bool _submenuOpensRight: true
    property real _submenuPointerCenterY: 24

    function submenuKeyForItem(item) {
        return item ? (item.action || item.text || "") : ""
    }

    function openWithSubmenuItems(apps, mime) {
        var items = []
        for (var i = 0; i < apps.length; ++i) {
            items.push({
                text: apps[i].name,
                shortcut: "",
                action: apps[i].isDefault ? "openwith" : "openwith",
                desktopFile: apps[i].desktopFile || "",
                iconName: apps[i].iconName || "",
                checked: !!apps[i].isDefault
            })
        }
        // "Other Application..."
        items.push({ separator: true })
        items.push({
            text: "Other Application\u2026",
            shortcut: "",
            action: "chooseapp",
            icon: "Search",
            mimeType: mime
        })
        return items
    }

    function popup(x, y) {
        closeSubmenu(true)
        _pendingX = x
        _pendingY = y
        _pendingPopup = true

        // Make visible so model builds and layout happens, but keep container invisible
        menuContainer.opacity = 0
        menuContainer.scale = 0.88
        root.visible = true
    }

    // Once menuColumn has its real height, position and animate
    Connections {
        target: menuColumn
        function onHeightChanged() {
            if (!root._pendingPopup) return
            root._pendingPopup = false
            root._reposition()
        }
    }

    function _reposition() {
        var menuW = menuColumn.width + 12
        var menuH = menuColumn.height + 12
        var winW = Window.width
        var winH = Window.height

        var posX = _pendingX
        if (posX + menuW + 8 > winW)
            posX = winW - menuW - 8
        posX = Math.max(8, posX)

        var posY = _pendingY
        if (posY + menuH + 8 > winH)
            posY = _pendingY - menuH
        posY = Math.max(8, Math.min(posY, winH - menuH - 8))

        menuContainer.x = posX
        menuContainer.y = posY
        menuContainer.transformOrigin = (_pendingY === posY) ? Item.TopLeft : Item.BottomLeft

        openAnim.start()
    }

    function _positionSubmenu() {
        if (!activeSubmenuSource)
            return

        var submenuBodyW = submenuColumn.width + 12
        var submenuW = submenuBodyW + root._submenuPointerDepth
        var submenuH = submenuColumn.height + 12
        var winW = root.width > 0 ? root.width : Window.width
        var winH = root.height > 0 ? root.height : Window.height
        var viewportMargin = 8
        var sourcePos = activeSubmenuSource.mapToItem(root, 0, 0)
        var sourceCenterY = sourcePos.y + activeSubmenuSource.height / 2
        var gap = _submenuGap
        var preferRight = sourcePos.x + activeSubmenuSource.width + gap + submenuBodyW + viewportMargin <= winW

        var posX = preferRight
            ? sourcePos.x + activeSubmenuSource.width + gap - root._submenuPointerDepth
            : sourcePos.x - submenuBodyW - gap
        posX = Math.max(viewportMargin, Math.min(posX, winW - submenuW - viewportMargin))

        var bubbleRadius = Math.min(Theme.radiusLarge, (submenuH / 2) - 1)
        var pointerHalf = root._submenuPointerBase / 2
        var pointerInset = bubbleRadius + pointerHalf + 2
        var pointerMin = Math.max(pointerHalf + 2, pointerInset)
        var pointerMax = Math.max(pointerMin, submenuH - pointerInset)
        var preferredPointerCenter = Math.max(pointerMin,
                                              Math.min(activeSubmenuSource.height / 2 + 6,
                                                       pointerMax))
        var posY = sourceCenterY - preferredPointerCenter
        posY = Math.max(viewportMargin,
                        Math.min(posY, winH - submenuH - viewportMargin))

        _submenuOpensRight = preferRight
        _submenuStartOffset = preferRight ? -10 : 10
        _submenuPointerCenterY = Math.max(pointerMin,
                                          Math.min(sourceCenterY - posY,
                                                   pointerMax))
        submenuContainer.transformOrigin = preferRight ? Item.TopLeft : Item.TopRight
        submenuContainer.x = posX
        submenuContainer.y = posY
    }

    function _showPendingSubmenu() {
        if (!_pendingSubmenuPopup || !submenuVisible)
            return

        _pendingSubmenuPopup = false
        _positionSubmenu()

        if (_animateSubmenuOnShow) {
            submenuContainer.opacity = 0
            submenuContainer.scale = 0.98
            submenuContainer.xOffset = _submenuStartOffset
            submenuOpenAnim.restart()
        } else {
            submenuContainer.opacity = 1
            submenuContainer.scale = 1
            submenuContainer.xOffset = 0
        }
    }

    function openSubmenu(item, sourceItem) {
        if (!item || !item.submenuItems || item.submenuItems.length === 0 || !sourceItem) {
            closeSubmenu(true)
            return
        }

        submenuCloseAnim.stop()
        submenuOpenAnim.stop()

        activeSubmenuKey = submenuKeyForItem(item)
        activeSubmenuSource = sourceItem
        submenuItems = item.submenuItems
        submenuVisible = true
        _pendingSubmenuPopup = true
        _animateSubmenuOnShow = submenuContainer.opacity <= 0.001
        Qt.callLater(_showPendingSubmenu)
    }

    function toggleSubmenu(item, sourceItem) {
        var key = submenuKeyForItem(item)
        if (submenuVisible && activeSubmenuKey === key) {
            closeSubmenu(true)
            return
        }

        openSubmenu(item, sourceItem)
    }

    function closeSubmenu(immediate) {
        if (immediate === undefined)
            immediate = false

        _pendingSubmenuPopup = false
        activeSubmenuKey = ""
        activeSubmenuSource = null

        if (immediate) {
            submenuOpenAnim.stop()
            submenuCloseAnim.stop()
            submenuVisible = false
            submenuItems = []
            submenuContainer.opacity = 0
            submenuContainer.scale = 0.98
            submenuContainer.xOffset = 0
            return
        }

        if (!submenuVisible && !submenuOpenAnim.running)
            return

        submenuVisible = false
        submenuOpenAnim.stop()
        submenuCloseAnim.restart()
    }

    function close() {
        closeSubmenu(true)
        closeAnim.start()
    }

    // ── Open animation ────────────────────────────────────────────────────
    ParallelAnimation {
        id: openAnim
        NumberAnimation {
            target: menuContainer; property: "opacity"
            from: 0; to: 1; duration: 180
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: menuContainer; property: "scale"
            from: 0.88; to: 1; duration: 250
            easing.type: Easing.OutBack
            easing.overshoot: 0.8
        }
        NumberAnimation {
            target: menuContainer; property: "yOffset"
            from: -8; to: 0; duration: 220
            easing.type: Easing.OutCubic
        }
    }

    // ── Close animation ───────────────────────────────────────────────────
    SequentialAnimation {
        id: closeAnim
        ParallelAnimation {
            NumberAnimation {
                target: menuContainer; property: "opacity"
                to: 0; duration: 120
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: menuContainer; property: "scale"
                to: 0.92; duration: 120
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: menuContainer; property: "yOffset"
                to: -4; duration: 120
                easing.type: Easing.InCubic
            }
        }
        ScriptAction { script: root.visible = false }
    }

    ParallelAnimation {
        id: submenuOpenAnim
        NumberAnimation {
            target: submenuContainer; property: "opacity"
            from: 0; to: 1; duration: 150
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: submenuContainer; property: "scale"
            from: 0.98; to: 1; duration: 170
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: submenuContainer; property: "xOffset"
            from: root._submenuStartOffset; to: 0; duration: 170
            easing.type: Easing.OutCubic
        }
    }

    ParallelAnimation {
        id: submenuCloseAnim
        onFinished: {
            if (!root.submenuVisible)
                root.submenuItems = []
        }
        NumberAnimation {
            target: submenuContainer; property: "opacity"
            to: 0; duration: 110
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: submenuContainer; property: "scale"
            to: 0.98; duration: 110
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: submenuContainer; property: "xOffset"
            to: root._submenuStartOffset; duration: 110
            easing.type: Easing.InCubic
        }
    }

    // Click outside to close
    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
        onWheel: (wheel) => { wheel.accepted = true }
    }

    // ── Menu container ────────────────────────────────────────────────────
    Item {
            id: menuContainer
            x: 0
            y: 0
            width: menuColumn.width + 12
        height: menuColumn.height + 12

        opacity: 0
        scale: 0.88
        transformOrigin: Item.TopLeft

        // Vertical slide offset driven by animation
        property real yOffset: 0
        transform: Translate { y: menuContainer.yOffset }

        // Background
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLarge
            color: Theme.crust
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
            border.width: 1
        }

        // ── Menu items with staggered entrance ──────────────────────────
        Column {
            id: menuColumn
            anchors.centerIn: parent
            width: root.menuWidth
            spacing: 2

            Repeater {
                model: root.visible ? root.buildModel() : []
                delegate: Loader {
                    id: delegateLoader
                    width: menuColumn.width
                    sourceComponent: modelData.separator ? separatorComponent
                                   : modelData.isSubmenu ? submenuTriggerComponent
                                   : itemComponent
                    property var itemData: modelData
                    property int itemIndex: index
                }
            }
        }
    }

    Item {
        id: submenuContainer
        x: 0
        y: 0
        property real bodyWidth: submenuColumn.width + 12
        property real bodyHeight: submenuColumn.height + 12
        property real bodyX: root._submenuOpensRight ? root._submenuPointerDepth : 0
        property real bodyRight: bodyX + bodyWidth
        property real bubbleRadius: Math.min(Theme.radiusLarge, (height / 2) - 1)
        property real pointerHalf: root._submenuPointerBase / 2
        property real pointerTop: root._submenuPointerCenterY - pointerHalf
        property real pointerBottom: root._submenuPointerCenterY + pointerHalf
        width: bodyWidth + root._submenuPointerDepth
        height: bodyHeight
        visible: root.submenuVisible || submenuOpenAnim.running || submenuCloseAnim.running
        opacity: 0
        scale: 0.98
        transformOrigin: Item.TopLeft
        property real xOffset: 0
        transform: Translate { x: submenuContainer.xOffset }

        Behavior on x {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }
        Behavior on y {
            NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
        }

        Shape {
            anchors.fill: parent
            visible: root._submenuOpensRight

            ShapePath {
                fillColor: Theme.crust
                strokeColor: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                strokeWidth: 1
                joinStyle: ShapePath.MiterJoin
                capStyle: ShapePath.FlatCap
                startX: submenuContainer.bodyX + submenuContainer.bubbleRadius
                startY: 0.5

                PathLine { x: submenuContainer.bodyRight - submenuContainer.bubbleRadius; y: 0.5 }
                PathQuad {
                    x: submenuContainer.bodyRight - 0.5
                    y: submenuContainer.bubbleRadius + 0.5
                    controlX: submenuContainer.bodyRight - 0.5
                    controlY: 0.5
                }
                PathLine { x: submenuContainer.bodyRight - 0.5; y: submenuContainer.height - submenuContainer.bubbleRadius - 0.5 }
                PathQuad {
                    x: submenuContainer.bodyRight - submenuContainer.bubbleRadius
                    y: submenuContainer.height - 0.5
                    controlX: submenuContainer.bodyRight - 0.5
                    controlY: submenuContainer.height - 0.5
                }
                PathLine { x: submenuContainer.bodyX + submenuContainer.bubbleRadius; y: submenuContainer.height - 0.5 }
                PathQuad {
                    x: submenuContainer.bodyX + 0.5
                    y: submenuContainer.height - submenuContainer.bubbleRadius - 0.5
                    controlX: submenuContainer.bodyX + 0.5
                    controlY: submenuContainer.height - 0.5
                }
                PathLine { x: submenuContainer.bodyX + 0.5; y: submenuContainer.pointerBottom }
                PathLine { x: 0.5; y: root._submenuPointerCenterY }
                PathLine { x: submenuContainer.bodyX + 0.5; y: submenuContainer.pointerTop }
                PathLine { x: submenuContainer.bodyX + 0.5; y: submenuContainer.bubbleRadius + 0.5 }
                PathQuad {
                    x: submenuContainer.bodyX + submenuContainer.bubbleRadius
                    y: 0.5
                    controlX: submenuContainer.bodyX + 0.5
                    controlY: 0.5
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: !root._submenuOpensRight

            ShapePath {
                fillColor: Theme.crust
                strokeColor: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                strokeWidth: 1
                joinStyle: ShapePath.MiterJoin
                capStyle: ShapePath.FlatCap
                startX: submenuContainer.bubbleRadius
                startY: 0.5

                PathLine { x: submenuContainer.bodyRight - submenuContainer.bubbleRadius; y: 0.5 }
                PathQuad {
                    x: submenuContainer.bodyRight - 0.5
                    y: submenuContainer.bubbleRadius + 0.5
                    controlX: submenuContainer.bodyRight - 0.5
                    controlY: 0.5
                }
                PathLine { x: submenuContainer.bodyRight - 0.5; y: submenuContainer.pointerTop }
                PathLine { x: submenuContainer.width - 0.5; y: root._submenuPointerCenterY }
                PathLine { x: submenuContainer.bodyRight - 0.5; y: submenuContainer.pointerBottom }
                PathLine { x: submenuContainer.bodyRight - 0.5; y: submenuContainer.height - submenuContainer.bubbleRadius - 0.5 }
                PathQuad {
                    x: submenuContainer.bodyRight - submenuContainer.bubbleRadius
                    y: submenuContainer.height - 0.5
                    controlX: submenuContainer.bodyRight - 0.5
                    controlY: submenuContainer.height - 0.5
                }
                PathLine { x: submenuContainer.bubbleRadius; y: submenuContainer.height - 0.5 }
                PathQuad {
                    x: 0.5
                    y: submenuContainer.height - submenuContainer.bubbleRadius - 0.5
                    controlX: 0.5
                    controlY: submenuContainer.height - 0.5
                }
                PathLine { x: 0.5; y: submenuContainer.bubbleRadius + 0.5 }
                PathQuad {
                    x: submenuContainer.bubbleRadius
                    y: 0.5
                    controlX: 0.5
                    controlY: 0.5
                }
            }
        }

        Column {
            id: submenuColumn
            x: submenuContainer.bodyX + 6
            y: 6
            width: root.menuWidth
            spacing: 2

            Repeater {
                model: submenuContainer.visible ? root.submenuItems : []
                delegate: Loader {
                    width: submenuColumn.width
                    sourceComponent: modelData.separator ? submenuSeparatorComponent : submenuItemComponent
                    property var subItemData: modelData
                }
            }
        }
    }

    Connections {
        target: submenuColumn
        function onHeightChanged() {
            if (root._pendingSubmenuPopup)
                root._showPendingSubmenu()
            else if (root.submenuVisible)
                root._positionSubmenu()
        }
    }

    function buildModel() {
        var items = []
        if (hasCustomItems) {
            return customItems
        }
        if (!isEmptySpace && targetPath !== "") {
            items.push({ text: "Open", shortcut: "Return", action: "open", icon: "ExternalLink" })
            if (targetIsDir)
                items.push({ text: "Open in New Tab", shortcut: "", action: "opennewtab", icon: "Folder" })
            if (!splitViewEnabled && !isTrashView) {
                items.push({
                    text: targetIsDir ? "Open in Split View" : "Split View Here",
                    shortcut: "",
                    action: targetIsDir ? "split_open" : "split_here",
                    icon: "SquareSplitHorizontal"
                })
            } else if (splitViewEnabled) {
                items.push({ text: "Close Split View", shortcut: "", action: "close_split", icon: "SquareSplitHorizontal" })
            }
            if (!targetIsDir && fileModel) {
                var props = fileModel.fileProperties(targetPath)
                var mime = props["mimeType"] || ""
                if (mime !== "") {
                    var apps = fileModel.availableApps(mime)
                    if (apps.length > 0)
                        items.push({ text: "Open With", shortcut: "", action: "openwith_toggle", isSubmenu: true, icon: "ExternalLink", submenuItems: openWithSubmenuItems(apps, mime) })
                    else
                        items.push({ text: "Open With\u2026", shortcut: "", action: "chooseapp_direct", icon: "ExternalLink", mimeType: mime })
                }
            }
            if (targetIsDir && !isTrashView && !remoteContext)
                items.push({ text: "Open in Terminal", shortcut: "", action: "terminal", icon: "Terminal" })
            items.push({ separator: true })
            if (isTrashView) {
                items.push({ text: "Restore", shortcut: "", action: "restore", icon: "Undo" })
                items.push({ text: "Delete Permanently", shortcut: "Delete", action: "delete", icon: "Trash", destructive: true })
            } else {
                items.push({ text: "Cut", shortcut: "Ctrl+X", action: "cut", icon: "Scissors" })
                items.push({ text: "Copy", shortcut: "Ctrl+C", action: "copy", icon: "Copy" })
                items.push({ text: "Copy Path", shortcut: "", action: "copypath", icon: "CopyPath" })
                items.push({ separator: true })

                // Compress submenu — always available for files/folders
                if (!remoteContext) {
                    items.push({ text: "Compress", shortcut: "", action: "compress_toggle", isSubmenu: true, icon: "FolderArchive",
                        submenuItems: [
                            { text: "ZIP", shortcut: "", action: "compress_zip" },
                            { text: "7z", shortcut: "", action: "compress_7z" },
                            { text: "tar.gz", shortcut: "", action: "compress_targz" },
                            { text: "tar.xz", shortcut: "", action: "compress_tarxz" },
                            { text: "tar.bz2", shortcut: "", action: "compress_tarbz2" },
                            { text: "tar", shortcut: "", action: "compress_tar" }
                        ]
                    })
                }

                // Extract option for archives
                if (!remoteContext && !targetIsDir && fileOps.isArchive(targetPath))
                    items.push({ text: "Extract Here", shortcut: "", action: "extract", icon: "PackageOpen" })

                items.push({ separator: true })
                items.push({
                    text: effectivePaths.length > 1 ? "Bulk Rename..." : "Rename...",
                    shortcut: "F2",
                    action: effectivePaths.length > 1 ? "bulkrename" : "rename",
                    icon: "FolderPen"
                })
                items.push({ text: "Move to Trash", shortcut: "Delete", action: "trash", icon: "Trash", destructive: true })
            }
            items.push({ separator: true })
            items.push({ text: "Properties", shortcut: "", action: "properties", icon: "Info" })
        } else {
            items.push({ text: "Select All", shortcut: "Ctrl+A", action: "selectall", icon: "Check" })
            items.push({ separator: true })
            items.push({ text: "View", shortcut: "", action: "view_toggle", isSubmenu: true, icon: "Eye",
                submenuItems: [
                    { text: "Grid", shortcut: "Ctrl+1", action: "view_grid", checked: currentViewMode === "grid", icon: "Grid" },
                    { text: "Miller", shortcut: "Ctrl+2", action: "view_miller", checked: currentViewMode === "miller", icon: "Columns" },
                    { text: "Detailed", shortcut: "Ctrl+3", action: "view_detailed", checked: currentViewMode === "detailed", icon: "AlignJustify" }
                ]
            })
            items.push({ text: "Sort By", shortcut: "", action: "sort_toggle", isSubmenu: true, icon: "SlidersH",
                submenuItems: [
                    { text: "Name", shortcut: "", action: "sort_name", checked: currentSortBy === "name" },
                    { text: "Size", shortcut: "", action: "sort_size", checked: currentSortBy === "size" },
                    { text: "Date Modified", shortcut: "", action: "sort_modified", checked: currentSortBy === "modified" },
                    { text: "Type", shortcut: "", action: "sort_type", checked: currentSortBy === "type" },
                    { separator: true },
                    { text: "Ascending", shortcut: "", action: "sort_asc", checked: currentSortAscending, icon: "ChevronUp" },
                    { text: "Descending", shortcut: "", action: "sort_desc", checked: !currentSortAscending, icon: "ChevronDown" }
                ]
            })
            if (!isTrashView) {
                items.push({ separator: true })
                items.push({ text: "New Folder...", shortcut: "Shift+Ctrl+N", action: "newfolder", icon: "Folder" })
                items.push({ text: "New File...", shortcut: "", action: "newfile", icon: "FileText" })
                items.push({ separator: true })
                if (clipboard.hasContent || (!remoteContext && fileOps.hasClipboardImage())) {
                    items.push({
                        text: clipboard.hasContent ? "Paste" : "Paste Image",
                        shortcut: "Ctrl+V",
                        action: "paste",
                        icon: clipboard.hasContent ? "Clipboard" : "Image"
                    })
                }
                if (!splitViewEnabled)
                    items.push({ text: "Split View Here", shortcut: "", action: "split_here", icon: "SquareSplitHorizontal" })
                else
                    items.push({ text: "Close Split View", shortcut: "", action: "close_split", icon: "SquareSplitHorizontal" })
                items.push({ separator: true })
                if (!remoteContext)
                    items.push({ text: "Open in Terminal", shortcut: "", action: "terminal", icon: "Terminal" })
                items.push({ text: "Properties", shortcut: "", action: "properties", icon: "Info" })
            } else {
                items.push({ separator: true })
                items.push({ text: "Empty Trash", shortcut: "", action: "emptytrash", icon: "Trash", destructive: true })
            }
        }
        return items
    }

    function executeAction(action, extraData, mimeType) {
        root.close()
        switch (action) {
        case "open": openRequested(targetPath, targetIsDir || isEmptySpace); break
        case "opennewtab": openInNewTabRequested(targetPath); break
        case "openwith": openWithRequested(targetPath, extraData); break
        case "setdefault": if (mimeType && extraData) setDefaultAppRequested(mimeType, extraData); break
        case "chooseapp": chooseAppRequested(targetPath, mimeType || ""); break
        case "chooseapp_direct": chooseAppRequested(targetPath, mimeType || ""); break
        case "cut": cutRequested(effectivePaths); break
        case "copy": copyRequested(effectivePaths); break
        case "copypath": copyPathRequested(targetPath); break
        case "rename": renameRequested(targetPath); break
        case "bulkrename": bulkRenameRequested(effectivePaths); break
        case "trash": trashRequested(effectivePaths); break
        case "restore": restoreRequested(effectivePaths); break
        case "delete": deleteRequested(effectivePaths); break
        case "paste": pasteRequested(effectiveDir); break
        case "selectall": selectAllRequested(); break
        case "terminal": openInTerminalRequested(effectiveDir); break
        case "newfolder": newFolderRequested(effectiveDir); break
        case "newfile": newFileRequested(effectiveDir); break
        case "properties": propertiesRequested(targetPath); break
        case "split_open": splitViewRequested(targetPath); break
        case "split_here": splitViewRequested(effectiveDir); break
        case "close_split": customActionRequested("close_split"); break
        case "view_grid": viewModeRequested("grid"); break
        case "view_miller": viewModeRequested("miller"); break
        case "view_detailed": viewModeRequested("detailed"); break
        case "sort_name": sortRequested("name", currentSortAscending); break
        case "sort_size": sortRequested("size", currentSortAscending); break
        case "sort_modified": sortRequested("modified", currentSortAscending); break
        case "sort_type": sortRequested("type", currentSortAscending); break
        case "sort_asc": sortRequested(currentSortBy, true); break
        case "sort_desc": sortRequested(currentSortBy, false); break
        case "compress_zip": fileOps.compressFiles(effectivePaths, "zip"); break
        case "compress_7z": fileOps.compressFiles(effectivePaths, "7z"); break
        case "compress_targz": fileOps.compressFiles(effectivePaths, "tar.gz"); break
        case "compress_tarxz": fileOps.compressFiles(effectivePaths, "tar.xz"); break
        case "compress_tarbz2": fileOps.compressFiles(effectivePaths, "tar.bz2"); break
        case "compress_tar": fileOps.compressFiles(effectivePaths, "tar"); break
        case "extract": fileOps.extractArchive(targetPath, effectiveDir); break
        case "emptytrash": emptyTrashRequested(); break
        default: customActionRequested(action); break
        }
    }

    Component {
        id: itemComponent
        Rectangle {
            height: 32
            width: parent ? parent.width : 260
            radius: Theme.radiusMedium
            color: itemMa.containsMouse
                ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                : "transparent"
            Behavior on color {
                ColorAnimation { duration: 100; easing.type: Easing.OutCubic }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                Loader {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignVCenter
                    active: itemData && itemData.icon
                    source: (itemData && itemData.icon) ? "../icons/Icon" + itemData.icon + ".qml" : ""
                    onLoaded: {
                        item.size = 16
                        item.color = Qt.binding(() => itemData && itemData.destructive ? Theme.error : Theme.muted)
                    }
                }
                Text {
                    text: itemData ? itemData.text : ""
                    font.pointSize: Theme.fontNormal
                    color: itemData && itemData.destructive ? Theme.error : Theme.text
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    text: itemData ? (itemData.shortcut || "") : ""
                    font.pointSize: Theme.fontSmall
                    color: Theme.muted
                    visible: text !== ""
                    verticalAlignment: Text.AlignVCenter
                }
            }
            MouseArea {
                id: itemMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: root.closeSubmenu(true)
                onClicked: {
                    if (itemData && itemData.action)
                        root.executeAction(itemData.action, itemData.desktopFile || "", itemData.mimeType || "")
                }
            }
        }
    }

    // ── Side submenu trigger row ───────────────────────────────────────────
    Component {
        id: submenuTriggerComponent
        Rectangle {
            id: submenuTrigger
            height: 32
            width: parent ? parent.width : 260
            radius: Theme.radiusMedium
            readonly property bool isActive: root.submenuVisible && root.activeSubmenuKey === root.submenuKeyForItem(itemData)
            color: (submenuMa.containsMouse || isActive)
                ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                : "transparent"
            Behavior on color {
                ColorAnimation { duration: 100; easing.type: Easing.OutCubic }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8
                Loader {
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    Layout.alignment: Qt.AlignVCenter
                    active: itemData && itemData.icon
                    source: (itemData && itemData.icon) ? "../icons/Icon" + itemData.icon + ".qml" : ""
                    onLoaded: {
                        item.size = 16
                        item.color = Qt.binding(() => submenuTrigger.isActive ? Theme.text : Theme.muted)
                    }
                }
                Text {
                    text: itemData ? itemData.text : ""
                    font.pointSize: Theme.fontNormal
                    color: Theme.text
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    text: itemData ? (itemData.shortcut || "") : ""
                    font.pointSize: Theme.fontSmall
                    color: Theme.muted
                    visible: text !== ""
                    verticalAlignment: Text.AlignVCenter
                }
                IconChevronRight {
                    size: 14
                    color: submenuTrigger.isActive ? Theme.text : Theme.muted
                    Layout.alignment: Qt.AlignVCenter
                }
            }
            MouseArea {
                id: submenuMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onEntered: root.openSubmenu(itemData, submenuTrigger)
                onClicked: root.openSubmenu(itemData, submenuTrigger)
            }
        }
    }

    Component {
        id: submenuItemComponent
        Rectangle {
            width: parent ? parent.width : 260
            height: 30
            radius: Theme.radiusMedium
            opacity: 1
            color: subItemMa.containsMouse
                ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                : "transparent"
            Behavior on color {
                ColorAnimation { duration: 100; easing.type: Easing.OutCubic }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 24
                anchors.rightMargin: 12
                spacing: 8
                Image {
                    source: subItemData && subItemData.iconName ? ("image://icon/" + subItemData.iconName) : ""
                    sourceSize: Qt.size(18, 18)
                    Layout.preferredWidth: visible ? 18 : 0
                    Layout.preferredHeight: 18
                    Layout.alignment: Qt.AlignVCenter
                    visible: !!(subItemData && subItemData.iconName) && status === Image.Ready
                }
                Loader {
                    Layout.preferredWidth: active ? 14 : 0
                    Layout.preferredHeight: 14
                    Layout.alignment: Qt.AlignVCenter
                    active: !!(subItemData && subItemData.icon)
                    source: (subItemData && subItemData.icon) ? "../icons/Icon" + subItemData.icon + ".qml" : ""
                    onLoaded: {
                        item.size = 14
                        item.color = Qt.binding(() => Theme.muted)
                    }
                }
                Text {
                    text: subItemData ? subItemData.text : ""
                    font.pointSize: Theme.fontSmall
                    color: Theme.text
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    text: subItemData ? (subItemData.shortcut || "") : ""
                    font.pixelSize: 11
                    color: Theme.muted
                    visible: text !== ""
                    verticalAlignment: Text.AlignVCenter
                }
                IconCheck {
                    visible: subItemData ? !!subItemData.checked : false
                    size: 14
                    color: Theme.accent
                    Layout.alignment: Qt.AlignVCenter
                }
            }
            MouseArea {
                id: subItemMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (subItemData && subItemData.action)
                        root.executeAction(subItemData.action, subItemData.desktopFile || "", subItemData.mimeType || "")
                }
            }
        }
    }

    Component {
        id: submenuSeparatorComponent
        Item {
            height: 9
            width: parent ? parent.width : 260
            Rectangle {
                anchors.centerIn: parent
                width: parent.width - 32
                height: 1
                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
            }
        }
    }

    Component {
        id: separatorComponent
        Item {
            height: 9
            width: parent ? parent.width : 260
            Rectangle {
                anchors.centerIn: parent
                width: parent.width - 16
                height: 1
                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
            }
        }
    }
}
