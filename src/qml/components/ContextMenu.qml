import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

// Custom context menu — Hyprland compositor handles blur via windowrule
Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 9999

    property string targetPath: ""
    property bool targetIsDir: false
    property bool isEmptySpace: false
    property var selectedPaths: []
    property var customItems: []
    property var contextData: ({})
    property int menuWidth: 260
    property bool splitViewEnabled: false
    property bool isTrashView: false

    signal openRequested(string path)
    signal openWithRequested(string path, string desktopFile)
    signal cutRequested(var paths)
    signal copyRequested(var paths)
    signal pasteRequested(string destPath)
    signal copyPathRequested(string path)
    signal renameRequested(string path)
    signal trashRequested(var paths)
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

    function popup(x, y) {
        openWithApps = []
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

    function close() {
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
                                   : modelData.isOpenWith ? openWithComponent
                                   : modelData.isSubmenu ? submenuComponent
                                   : itemComponent
                    property var itemData: modelData
                    property int itemIndex: index
                }
            }
        }
    }

    // ── Open With apps cache (populated when menu opens) ──────────────────
    property var openWithApps: []

    function buildModel() {
        var items = []
        if (hasCustomItems) {
            openWithApps = []
            return customItems
        }
        if (!isEmptySpace && targetPath !== "") {
            items.push({ text: "Open", shortcut: "Return", action: "open", icon: "ExternalLink" })
            if (!splitViewEnabled) {
                items.push({
                    text: targetIsDir ? "Open in Split View" : "Split View Here",
                    shortcut: "",
                    action: targetIsDir ? "split_open" : "split_here",
                    icon: "SquareSplitHorizontal"
                })
            }
            if (!targetIsDir && fileModel) {
                var props = fileModel.fileProperties(targetPath)
                var mime = props["mimeType"] || ""
                if (mime !== "") {
                    var apps = fileModel.availableApps(mime)
                    if (apps.length > 0) {
                        openWithApps = apps
                        items.push({ text: "Open With", shortcut: "", action: "openwith_toggle", isOpenWith: true })
                    } else {
                        openWithApps = []
                    }
                } else {
                    openWithApps = []
                }
            } else {
                openWithApps = []
            }
            if (targetIsDir)
                items.push({ text: "Open in Terminal", shortcut: "", action: "terminal", icon: "Terminal" })
            items.push({ separator: true })
            items.push({ text: "Cut", shortcut: "Ctrl+X", action: "cut", icon: "Scissors" })
            items.push({ text: "Copy", shortcut: "Ctrl+C", action: "copy", icon: "Copy" })
            items.push({ text: "Copy Path", shortcut: "", action: "copypath", icon: "CopyPath" })
            items.push({ separator: true })

            // Compress submenu — always available for files/folders
            items.push({ text: "Compress", shortcut: "", action: "compress_toggle", isSubmenu: true, icon: "FolderArchive",
                submenuItems: [
                    { text: "ZIP", shortcut: "", action: "compress_zip" },
                    { text: "tar.gz", shortcut: "", action: "compress_targz" },
                    { text: "tar.xz", shortcut: "", action: "compress_tarxz" },
                    { text: "tar.bz2", shortcut: "", action: "compress_tarbz2" },
                    { text: "tar", shortcut: "", action: "compress_tar" }
                ]
            })

            // Extract option for archives
            if (!targetIsDir && fileOps.isArchive(targetPath))
                items.push({ text: "Extract Here", shortcut: "", action: "extract", icon: "PackageOpen" })

            items.push({ separator: true })
            items.push({ text: "Rename...", shortcut: "F2", action: "rename", icon: "FolderPen" })
            if (isTrashView)
                items.push({ text: "Delete Permanently", shortcut: "Delete", action: "delete", icon: "Trash", destructive: true })
            else
                items.push({ text: "Move to Trash", shortcut: "Delete", action: "trash", icon: "Trash", destructive: true })
            items.push({ separator: true })
            items.push({ text: "Properties", shortcut: "", action: "properties", icon: "Info" })
        } else {
            items.push({ text: "New Folder...", shortcut: "Shift+Ctrl+N", action: "newfolder", icon: "Folder" })
            items.push({ text: "New File...", shortcut: "", action: "newfile", icon: "FileText" })
            items.push({ separator: true })
            if (clipboard.hasContent || fileOps.hasClipboardImage()) {
                items.push({
                    text: clipboard.hasContent ? "Paste" : "Paste Image",
                    shortcut: "Ctrl+V",
                    action: "paste",
                    icon: clipboard.hasContent ? "Clipboard" : "Image"
                })
            }
            if (!splitViewEnabled)
                items.push({ text: "Split View Here", shortcut: "", action: "split_here", icon: "SquareSplitHorizontal" })
            items.push({ text: "Select All", shortcut: "Ctrl+A", action: "selectall", icon: "Check" })
            items.push({ separator: true })
            items.push({ text: "View", shortcut: "", action: "view_toggle", isSubmenu: true, icon: "Eye",
                submenuItems: [
                    { text: "Grid", shortcut: "Ctrl+1", action: "view_grid", checked: currentViewMode === "grid", icon: "Grid" },
                    { text: "List", shortcut: "Ctrl+2", action: "view_list", checked: currentViewMode === "list", icon: "List" },
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
            items.push({ separator: true })
            items.push({ text: "Open in Terminal", shortcut: "", action: "terminal", icon: "Terminal" })
            items.push({ text: "Properties", shortcut: "", action: "properties", icon: "Info" })
            if (isTrashView) {
                items.push({ separator: true })
                items.push({ text: "Empty Trash", shortcut: "", action: "emptytrash", icon: "Trash", destructive: true })
            }
        }
        return items
    }

    function executeAction(action, extraData) {
        root.close()
        switch (action) {
        case "open": openRequested(targetPath); break
        case "openwith": openWithRequested(targetPath, extraData); break
        case "cut": cutRequested(effectivePaths); break
        case "copy": copyRequested(effectivePaths); break
        case "copypath": copyPathRequested(targetPath); break
        case "rename": renameRequested(targetPath); break
        case "trash": trashRequested(effectivePaths); break
        case "delete": deleteRequested(effectivePaths); break
        case "paste": pasteRequested(effectiveDir); break
        case "selectall": selectAllRequested(); break
        case "terminal": openInTerminalRequested(effectiveDir); break
        case "newfolder": newFolderRequested(effectiveDir); break
        case "newfile": newFileRequested(effectiveDir); break
        case "properties": propertiesRequested(targetPath); break
        case "split_open": splitViewRequested(targetPath); break
        case "split_here": splitViewRequested(effectiveDir); break
        case "view_grid": viewModeRequested("grid"); break
        case "view_list": viewModeRequested("list"); break
        case "view_detailed": viewModeRequested("detailed"); break
        case "sort_name": sortRequested("name", currentSortAscending); break
        case "sort_size": sortRequested("size", currentSortAscending); break
        case "sort_modified": sortRequested("modified", currentSortAscending); break
        case "sort_type": sortRequested("type", currentSortAscending); break
        case "sort_asc": sortRequested(currentSortBy, true); break
        case "sort_desc": sortRequested(currentSortBy, false); break
        case "compress_zip": fileOps.compressFiles(effectivePaths, "zip"); break
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
                onClicked: {
                    if (itemData && itemData.action)
                        root.executeAction(itemData.action, itemData.desktopFile || "")
                }
            }
        }
    }

    // ── Expandable "Open With" button + animated app list ────────────────
    Component {
        id: openWithComponent
        Column {
            id: openWithCol
            width: parent ? parent.width : 260

            property bool expanded: false

            // The "Open With" button row
            Rectangle {
                width: parent.width
                height: 32
                radius: Theme.radiusMedium
                color: owMa.containsMouse
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
                        source: "../icons/IconExternalLink.qml"
                        onLoaded: {
                            item.size = 16
                            item.color = Qt.binding(() => Theme.muted)
                        }
                    }
                    Text {
                        text: "Open With"
                        font.pointSize: Theme.fontNormal
                        color: Theme.text
                        Layout.fillWidth: true
                        verticalAlignment: Text.AlignVCenter
                    }
                    Item {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        Layout.alignment: Qt.AlignVCenter
                        rotation: openWithCol.expanded ? 0 : -90
                        Behavior on rotation {
                            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                        }
                        IconChevronDown {
                            anchors.centerIn: parent
                            size: 14
                            color: Theme.muted
                        }
                    }
                }
                MouseArea {
                    id: owMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: openWithCol.expanded = !openWithCol.expanded
                }
            }

            // Animated expandable app list
            Item {
                width: parent.width
                height: openWithCol.expanded ? appColumn.height : 0
                clip: true
                Behavior on height {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutCubic
                    }
                }

                Column {
                    id: appColumn
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: root.openWithApps
                        delegate: Rectangle {
                            width: appColumn.width
                            height: 30
                            radius: Theme.radiusMedium
                            opacity: openWithCol.expanded ? 1 : 0
                            Behavior on opacity {
                                NumberAnimation {
                                    duration: 150
                                    easing.type: Easing.OutCubic
                                }
                            }
                            color: appItemMa.containsMouse
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
                                    source: modelData.iconName ? ("image://icon/" + modelData.iconName) : ""
                                    sourceSize: Qt.size(18, 18)
                                    Layout.preferredWidth: 18
                                    Layout.preferredHeight: 18
                                    Layout.alignment: Qt.AlignVCenter
                                    visible: modelData.iconName && status === Image.Ready
                                }
                                Text {
                                    text: modelData.name
                                    font.pointSize: Theme.fontSmall
                                    color: Theme.text
                                    Layout.fillWidth: true
                                    verticalAlignment: Text.AlignVCenter
                                }
                                IconCheck {
                                    visible: modelData.isDefault
                                    size: 14
                                    color: Theme.accent
                                    Layout.alignment: Qt.AlignVCenter
                                }
                            }
                            MouseArea {
                                id: appItemMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.executeAction("openwith", modelData.desktopFile)
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Expandable submenu (View / Sort By) ─────────────────────────────
    Component {
        id: submenuComponent
        Column {
            id: submenuCol
            width: parent ? parent.width : 260

            property bool expanded: false

            // Header row
            Rectangle {
                width: parent.width
                height: 32
                radius: Theme.radiusMedium
                color: submenuMa.containsMouse
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
                            item.color = Qt.binding(() => Theme.muted)
                        }
                    }
                    Text {
                        text: itemData ? itemData.text : ""
                        font.pointSize: Theme.fontNormal
                        color: Theme.text
                        Layout.fillWidth: true
                        verticalAlignment: Text.AlignVCenter
                    }
                    Item {
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        Layout.alignment: Qt.AlignVCenter
                        rotation: submenuCol.expanded ? 0 : -90
                        Behavior on rotation {
                            NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                        }
                        IconChevronDown {
                            anchors.centerIn: parent
                            size: 14
                            color: Theme.muted
                        }
                    }
                }
                MouseArea {
                    id: submenuMa
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: submenuCol.expanded = !submenuCol.expanded
                }
            }

            // Animated expandable items
            Item {
                width: parent.width
                height: submenuCol.expanded ? submenuInnerCol.height : 0
                clip: true
                Behavior on height {
                    NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
                }

                Column {
                    id: submenuInnerCol
                    width: parent.width
                    spacing: 2

                    Repeater {
                        model: (itemData && itemData.submenuItems) ? itemData.submenuItems : []
                        delegate: Loader {
                            width: submenuInnerCol.width
                            sourceComponent: modelData.separator ? submenuSeparatorComponent : submenuItemComponent
                            property var subItemData: modelData
                        }
                    }
                }
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
                Loader {
                    Layout.preferredWidth: 14
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
                        root.executeAction(subItemData.action, "")
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
