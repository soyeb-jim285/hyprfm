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
    signal viewModeRequested(string mode)
    signal sortRequested(string column, bool ascending)

    property string currentViewMode: "grid"
    property string currentSortBy: "name"
    property bool currentSortAscending: true

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

    function popup(x, y) {
        openWithApps = []
        var menuW = menuColumn.width + 12
        var menuH = menuColumn.height + 12
        menuContainer.x = Math.min(x, root.width - menuW - 8)
        menuContainer.y = Math.min(y, root.height - menuH - 8)
        root.visible = true
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
            width: 260
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
        if (!isEmptySpace && targetPath !== "") {
            items.push({ text: "Open", shortcut: "Return", action: "open" })
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
                items.push({ text: "Open in Terminal", shortcut: "", action: "terminal" })
            items.push({ separator: true })
            items.push({ text: "Cut", shortcut: "Ctrl+X", action: "cut" })
            items.push({ text: "Copy", shortcut: "Ctrl+C", action: "copy" })
            items.push({ text: "Copy Path", shortcut: "", action: "copypath" })
            items.push({ separator: true })
            items.push({ text: "Rename...", shortcut: "F2", action: "rename" })
            items.push({ text: "Move to Trash", shortcut: "Delete", action: "trash" })
            items.push({ separator: true })
            items.push({ text: "Properties", shortcut: "", action: "properties" })
        } else {
            items.push({ text: "New Folder...", shortcut: "Shift+Ctrl+N", action: "newfolder" })
            items.push({ text: "New File...", shortcut: "", action: "newfile" })
            items.push({ separator: true })
            if (clipboard.hasContent)
                items.push({ text: "Paste", shortcut: "Ctrl+V", action: "paste" })
            items.push({ text: "Select All", shortcut: "Ctrl+A", action: "selectall" })
            items.push({ separator: true })
            items.push({ text: "View", shortcut: "", action: "view_toggle", isSubmenu: true,
                submenuItems: [
                    { text: "Grid", shortcut: "Ctrl+1", action: "view_grid", checked: currentViewMode === "grid" },
                    { text: "List", shortcut: "Ctrl+2", action: "view_list", checked: currentViewMode === "list" },
                    { text: "Detailed", shortcut: "Ctrl+3", action: "view_detailed", checked: currentViewMode === "detailed" }
                ]
            })
            items.push({ text: "Sort By", shortcut: "", action: "sort_toggle", isSubmenu: true,
                submenuItems: [
                    { text: "Name", shortcut: "", action: "sort_name", checked: currentSortBy === "name" },
                    { text: "Size", shortcut: "", action: "sort_size", checked: currentSortBy === "size" },
                    { text: "Date Modified", shortcut: "", action: "sort_modified", checked: currentSortBy === "modified" },
                    { text: "Type", shortcut: "", action: "sort_type", checked: currentSortBy === "type" },
                    { separator: true },
                    { text: "Ascending", shortcut: "", action: "sort_asc", checked: currentSortAscending },
                    { text: "Descending", shortcut: "", action: "sort_desc", checked: !currentSortAscending }
                ]
            })
            items.push({ separator: true })
            items.push({ text: "Open in Terminal", shortcut: "", action: "terminal" })
            items.push({ text: "Properties", shortcut: "", action: "properties" })
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
        case "view_grid": viewModeRequested("grid"); break
        case "view_list": viewModeRequested("list"); break
        case "view_detailed": viewModeRequested("detailed"); break
        case "sort_name": sortRequested("name", currentSortAscending); break
        case "sort_size": sortRequested("size", currentSortAscending); break
        case "sort_modified": sortRequested("modified", currentSortAscending); break
        case "sort_type": sortRequested("type", currentSortAscending); break
        case "sort_asc": sortRequested(currentSortBy, true); break
        case "sort_desc": sortRequested(currentSortBy, false); break
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
                spacing: 16
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
                    spacing: 16
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
                    spacing: 16
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
