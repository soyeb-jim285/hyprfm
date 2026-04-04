import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import HyprFM

Rectangle {
    id: root

    property string currentPath: ""
    property string trashPath: fsModel.homePath() + "/.local/share/Trash/files"
    property bool isRecentsView: false
    signal bookmarkClicked(string path)
    signal sidebarContextMenuRequested(var item, point position)
    signal recentsClicked()
    signal collapseClicked()
    signal featureHintRequested(string message)

    color: Theme.mantle
    clip: false

    Component { id: iconHome; IconHome { size: 18; color: Theme.subtext } }
    Component { id: iconClock; IconClock { size: 18; color: Theme.subtext } }
    Component { id: iconTrash; IconTrash { size: 18; color: Theme.subtext } }
    Component { id: iconImage; IconImage { size: 18; color: Theme.subtext } }
    Component { id: iconDownload; IconDownload { size: 18; color: Theme.subtext } }
    Component { id: iconGlobe; IconGlobe { size: 18; color: Theme.subtext } }
    Component { id: iconHardDrive; IconHardDrive { size: 18; color: Theme.subtext } }
    Component { id: iconHardDriveOff; IconHardDriveOff { size: 18; color: Theme.muted } }
    Component { id: iconFolder; IconFolder { size: 18; color: Theme.subtext } }

    // Inverse rounded corner — top right
    Shape {
        z: 1; width: Theme.radiusMedium; height: Theme.radiusMedium
        anchors.top: parent.top; anchors.left: parent.right
        ShapePath {
            fillColor: Theme.mantle; strokeColor: "transparent"
            startX: 0; startY: 0
            PathLine { x: Theme.radiusMedium; y: 0 }
            PathArc {
                x: 0; y: Theme.radiusMedium
                radiusX: Theme.radiusMedium; radiusY: Theme.radiusMedium
                direction: PathArc.Clockwise
            }
            PathLine { x: 0; y: 0 }
        }
    }

    // Inverse rounded corner — bottom right
    Shape {
        z: 1; width: Theme.radiusMedium; height: Theme.radiusMedium
        anchors.bottom: parent.bottom; anchors.left: parent.right
        ShapePath {
            fillColor: Theme.mantle; strokeColor: "transparent"
            startX: 0; startY: Theme.radiusMedium
            PathLine { x: Theme.radiusMedium; y: Theme.radiusMedium }
            PathArc {
                x: 0; y: 0
                radiusX: Theme.radiusMedium; radiusY: Theme.radiusMedium
                direction: PathArc.Clockwise
            }
            PathLine { x: 0; y: 0 }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // App header: "hyprfm" + collapse button
        Item {
            Layout.fillWidth: true
            height: 44

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing + 4
                anchors.verticalCenter: parent.verticalCenter
                text: "Hyprfm"
                color: Theme.text
                font.pointSize: Theme.fontLarge
                font.weight: Font.Bold
            }

            HoverRect {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing
                anchors.verticalCenter: parent.verticalCenter
                width: 28; height: 28
                onClicked: root.collapseClicked()
                IconPanelLeft { anchors.centerIn: parent; size: 16; color: Theme.subtext }
            }
        }

        // Quick access section
        Column {
            Layout.fillWidth: true

            // Quick access entries
            Repeater {
                model: ListModel {
                    ListElement { name: "Home"; iconType: "home" }
                    ListElement { name: "Recents"; iconType: "clock" }
                    ListElement { name: "Trash"; iconType: "trash" }
                    ListElement { name: "Network"; iconType: "globe" }
                    ListElement { name: "Pictures"; iconType: "image" }
                    ListElement { name: "Downloads"; iconType: "download" }
                }

                delegate: Rectangle {
                    id: quickAccessDelegate

                    readonly property string resolvedPath: {
                        const home = fsModel.homePath()
                        if (model.name === "Home") return home
                        if (model.name === "Recents") return ""
                        if (model.name === "Trash") return root.trashPath
                        if (model.name === "Network") return "network:///"
                        if (model.name === "Pictures") return home + "/Pictures"
                        if (model.name === "Downloads") return home + "/Downloads"
                        return ""
                    }

                    width: parent.width - Theme.spacing
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 32
                    readonly property bool isActive: {
                        if (model.name === "Recents") return root.isRecentsView
                        if (model.name === "Trash") return !root.isRecentsView && fileOps.isTrashPath(root.currentPath)
                        if (model.name === "Network") return !root.isRecentsView && fileOps.isRemotePath(root.currentPath)
                        if (resolvedPath === "") return false
                        return !root.isRecentsView && resolvedPath === root.currentPath
                    }

                    color: {
                        if (isActive) return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                        if (qaHoverArea.containsMouse) return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                        return "transparent"
                    }
                    radius: Theme.radiusSmall
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacing

                        Loader {
                            width: 18; height: 18
                            anchors.verticalCenter: parent.verticalCenter
                            sourceComponent: {
                                if (model.iconType === "home") return iconHome
                                if (model.iconType === "clock") return iconClock
                                if (model.iconType === "trash") return iconTrash
                                if (model.iconType === "monitor") return iconMonitor
                                if (model.iconType === "globe") return iconGlobe
                                if (model.iconType === "image") return iconImage
                                if (model.iconType === "download") return iconDownload
                                return iconHome
                            }
                        }

                        Text {
                            text: model.name
                            color: quickAccessDelegate.isActive ? Theme.accent : Theme.text
                            font.pointSize: Theme.fontNormal
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            width: parent.width - 32 - Theme.spacing
                        }
                    }

                    MouseArea {
                        id: qaHoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                var mapped = qaHoverArea.mapToItem(null, mouse.x, mouse.y)
                                root.sidebarContextMenuRequested({
                                    kind: "quickAccess",
                                    name: model.name,
                                    path: quickAccessDelegate.resolvedPath,
                                    isRecents: model.name === "Recents"
                                }, Qt.point(mapped.x, mapped.y))
                                return
                            }

                            if (model.name === "Recents")
                                root.recentsClicked()
                            else
                                root.bookmarkClicked(quickAccessDelegate.resolvedPath)
                        }
                    }
                }
            }
        }

        // Separator between quick access and bookmarks
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing
            Layout.rightMargin: Theme.spacing
            height: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
        }

        // Bookmarks section — drag folders to add, drag items to reorder
        Item {
            id: bookmarksSection
            Layout.fillWidth: true
            implicitHeight: bookmarksList.height

            readonly property int rowHeight: 32
            readonly property bool externalDragActive:
                bookmarkDropArea.containsDrag
                && bookmarkDropArea._extDropIndex >= 0
                && dragCurrentIndex < 0
            readonly property real externalGapHeight: externalDragActive ? rowHeight : 0
            readonly property int externalDropIndex: bookmarkDropArea._extDropIndex

            property int dragCurrentIndex: -1
            property string dragName: ""
            property real dragMouseY: 0
            property string externalDragName: ""
            property real externalDragMouseY: 0

            function dragUrls(data) {
                var urls = data.urls || []
                if ((!urls || urls.length === 0) && data.text)
                    urls = data.text.split("\n").filter(u => u.trim() !== "")
                return urls
            }

            function decodedPath(url) {
                var value = url.toString().replace(/\/$/, "")
                return value.startsWith("file://") ? decodeURIComponent(value.replace("file://", "")) : value
            }

            function displayName(path) {
                if (!path)
                    return ""
                var trimmed = path.replace(/\/$/, "")
                if (trimmed === "")
                    return "/"
                var parts = trimmed.split("/")
                return parts[parts.length - 1] || trimmed
            }

            function updateExternalDrop(drag) {
                var clampedY = Math.max(0, Math.min(drag.y, bookmarks.count * rowHeight))
                bookmarkDropArea._extDropIndex = Math.max(0,
                    Math.min(Math.round(clampedY / rowHeight), bookmarks.count))
                externalDragMouseY = Math.max(0, Math.min(drag.y, bookmarksList.height))

                var urls = dragUrls(drag)
                if (urls.length === 1)
                    externalDragName = displayName(decodedPath(urls[0]))
                else if (urls.length > 1)
                    externalDragName = urls.length + " items"
                else
                    externalDragName = "New bookmark"
            }

            function clearExternalDrop() {
                bookmarkDropArea._extDropIndex = -1
                externalDragName = ""
                externalDragMouseY = 0
            }

            // External drop zone for adding new bookmarks
            DropArea {
                id: bookmarkDropArea
                anchors.fill: parent
                keys: ["text/uri-list"]

                onEntered: (drag) => bookmarksSection.updateExternalDrop(drag)
                onPositionChanged: (drag) => bookmarksSection.updateExternalDrop(drag)
                onExited: bookmarksSection.clearExternalDrop()
                onDropped: (drop) => {
                    var insertAt = bookmarkDropArea._extDropIndex
                    var urls = bookmarksSection.dragUrls(drop)
                    bookmarksSection.clearExternalDrop()
                    for (var i = 0; i < urls.length; i++) {
                        var path = bookmarksSection.decodedPath(urls[i])
                        if (path !== "")
                            bookmarks.insertBookmark(path, insertAt >= 0 ? insertAt : bookmarks.count)
                    }
                    drop.accept()
                }
                property int _extDropIndex: -1
            }

            ListView {
                id: bookmarksList
                width: parent.width
                height: contentHeight + bookmarksSection.externalGapHeight
                interactive: false

                model: bookmarks

                add: Transition {
                    enabled: bookmarksSection.dragCurrentIndex < 0
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.OutCubic }
                        NumberAnimation { property: "scale"; from: 0.9; to: 1; duration: 250; easing.type: Easing.OutBack; easing.overshoot: 0.6 }
                    }
                }
                move: Transition {
                    NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutCubic }
                }
                displaced: Transition {
                    NumberAnimation { properties: "x,y"; duration: 150; easing.type: Easing.OutCubic }
                }

                delegate: Item {
                    width: bookmarksList.width
                    height: bookmarksSection.rowHeight

                    Rectangle {
                        id: bmDelegate
                        width: parent.width - Theme.spacing
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: bookmarksSection.rowHeight
                        y: bookmarksSection.externalDragActive && index >= bookmarksSection.externalDropIndex
                            ? bookmarksSection.rowHeight : 0
                        Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                        opacity: bookmarksSection.dragCurrentIndex === index ? 0.35 : 1.0
                        Behavior on opacity { NumberAnimation { duration: 120 } }

                        readonly property bool isActive:
                            !root.isRecentsView && model.path === root.currentPath

                        color: {
                            if (isActive) return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                            if (bmInteraction.hoverIndex === index && bookmarksSection.dragCurrentIndex < 0)
                                return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                            return "transparent"
                        }
                        radius: Theme.radiusSmall
                        Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacing
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacing
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacing

                            Loader {
                                width: 18; height: 18
                                anchors.verticalCenter: parent.verticalCenter
                                sourceComponent: iconFolder
                            }

                            Text {
                                text: model.name
                                color: bmDelegate.isActive ? Theme.accent : Theme.text
                                font.pointSize: Theme.fontNormal
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                width: parent.width - 18 - Theme.spacing
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: bookmarksSection.externalDragActive
                z: 120
                width: bookmarksList.width - Theme.spacing
                height: bookmarksSection.rowHeight
                radius: Theme.radiusSmall
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35)
                border.width: 1
                x: Theme.spacing / 2
                y: Math.max(0,
                            Math.min(bookmarksSection.externalDragMouseY - height / 2,
                                     bookmarksList.height - height))
                opacity: 0.95
                Behavior on y { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                Behavior on opacity { NumberAnimation { duration: 120 } }

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacing

                    Loader {
                        width: 18; height: 18
                        anchors.verticalCenter: parent.verticalCenter
                        sourceComponent: iconFolder
                    }

                    Text {
                        text: bookmarksSection.externalDragName
                        color: Theme.text
                        font.pointSize: Theme.fontNormal
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        width: parent.width - 18 - Theme.spacing
                    }
                }
            }

            // Single MouseArea handles hover, click, and drag for all bookmarks
            MouseArea {
                id: bmInteraction
                anchors.fill: bookmarksList
                z: 100
                hoverEnabled: true
                cursorShape: bookmarksSection.dragCurrentIndex >= 0 ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                acceptedButtons: Qt.LeftButton | Qt.RightButton

                property int hoverIndex: -1
                property int pressIndex: -1
                property real pressY: 0
                property bool isDragging: false
                property int pressButton: Qt.NoButton

                function idxAt(y) {
                    return Math.max(0, Math.min(Math.floor(y / bookmarksSection.rowHeight), bookmarks.count - 1))
                }

                onPositionChanged: (mouse) => {
                    hoverIndex = (mouse.y >= 0 && mouse.y < bookmarks.count * bookmarksSection.rowHeight)
                        ? idxAt(mouse.y) : -1

                    if (!pressed) return
                    if (pressButton !== Qt.LeftButton) return
                    if (!isDragging && Math.abs(mouse.y - pressY) > 6 && pressIndex >= 0) {
                        isDragging = true
                        bookmarksSection.dragCurrentIndex = pressIndex
                        bookmarksSection.dragName = bookmarks.data(
                            bookmarks.index(pressIndex, 0), 257 /* NameRole */) || ""
                    }
                    if (isDragging) {
                        bookmarksSection.dragMouseY = mouse.y
                        var target = idxAt(mouse.y)
                        if (target !== bookmarksSection.dragCurrentIndex) {
                            bookmarks.moveBookmark(bookmarksSection.dragCurrentIndex, target)
                            bookmarksSection.dragCurrentIndex = target
                        }
                    }
                }
                onPressed: (mouse) => {
                    pressIndex = idxAt(mouse.y)
                    pressY = mouse.y
                    isDragging = false
                    pressButton = mouse.button
                }
                onReleased: (mouse) => {
                    if (isDragging) {
                        bookmarksSection.dragCurrentIndex = -1
                        isDragging = false
                    } else if (mouse.button === Qt.LeftButton && pressIndex >= 0 && pressIndex < bookmarks.count) {
                        var path = bookmarks.data(
                            bookmarks.index(pressIndex, 0), 258 /* PathRole */) || ""
                        if (path) root.bookmarkClicked(path)
                    }
                    pressIndex = -1
                    pressButton = Qt.NoButton
                }
                onClicked: (mouse) => {
                    if (mouse.button !== Qt.RightButton)
                        return

                    var index = (mouse.y >= 0 && mouse.y < bookmarks.count * bookmarksSection.rowHeight)
                        ? idxAt(mouse.y) : -1
                    if (index < 0 || index >= bookmarks.count)
                        return

                    var path = bookmarks.data(bookmarks.index(index, 0), 258 /* PathRole */) || ""
                    var mapped = bmInteraction.mapToItem(null, mouse.x, mouse.y)
                    root.sidebarContextMenuRequested({
                        kind: "bookmark",
                        index: index,
                        name: bookmarks.data(bookmarks.index(index, 0), 257 /* NameRole */) || "",
                        path: path
                    }, Qt.point(mapped.x, mapped.y))
                }
                onExited: hoverIndex = -1
                onCanceled: {
                    bookmarksSection.dragCurrentIndex = -1
                    isDragging = false
                    pressIndex = -1
                    pressButton = Qt.NoButton
                }
            }

            // Ghost bookmark following cursor
            Rectangle {
                visible: bookmarksSection.dragCurrentIndex >= 0
                z: 200
                width: bookmarksList.width - Theme.spacing
                height: bookmarksSection.rowHeight
                radius: Theme.radiusSmall
                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15)
                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                border.width: 1
                x: Theme.spacing / 2
                y: Math.max(0,
                            Math.min(bookmarksSection.dragMouseY - height / 2,
                                     bookmarksList.height - height))
                opacity: 0.9

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacing

                    Loader {
                        width: 18; height: 18
                        anchors.verticalCenter: parent.verticalCenter
                        sourceComponent: iconFolder
                    }
                    Text {
                        text: bookmarksSection.dragName
                        color: Theme.text
                        font.pointSize: Theme.fontNormal
                    }
                }
            }
        }

        // Separator between bookmarks and devices
        Rectangle {
            visible: bookmarks.count > 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing
            Layout.rightMargin: Theme.spacing
            height: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
        }

        // Auto-navigate after mounting an unmounted device
        Connections {
            target: devices
            function onDeviceMounted(mountPoint) {
                root.bookmarkClicked(mountPoint)
            }
        }

        // Devices section
        Column {
            Layout.fillWidth: true

            Repeater {
                model: devices

                delegate: Rectangle {
                    id: deviceDelegate
                    width: parent.width - Theme.spacing
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 36
                    color: deviceHoverArea.containsMouse
                        ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                        : "transparent"
                    radius: Theme.radiusSmall
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                    Row {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.spacing
                        anchors.rightMargin: Theme.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacing

                        // Drive icon: mounted vs unmounted
                        Loader {
                            width: 18; height: 18
                            anchors.verticalCenter: parent.verticalCenter
                            sourceComponent: model.mounted ? iconHardDrive : iconHardDriveOff
                        }

                        // Right side: name + progress bar
                        Column {
                            width: parent.width - 18 - Theme.spacing
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3

                            Text {
                                text: model.deviceName
                                color: model.mounted ? Theme.text : Theme.muted
                                font.pointSize: Theme.fontNormal
                                elide: Text.ElideRight
                                width: parent.width
                            }

                            // Storage usage bar
                            Rectangle {
                                width: parent.width
                                height: 3
                                radius: 1.5
                                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)

                                Rectangle {
                                    width: model.mounted
                                        ? parent.width * Math.min(model.usagePercent / 100.0, 1.0)
                                        : 0
                                    height: parent.height
                                    radius: parent.radius
                                    color: model.usagePercent >= 90
                                        ? Theme.error
                                        : model.usagePercent >= 75
                                            ? Theme.warning
                                            : Theme.accent
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: deviceHoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                var mapped = deviceHoverArea.mapToItem(null, mouse.x, mouse.y)
                                root.sidebarContextMenuRequested({
                                    kind: "device",
                                    index: index,
                                    name: model.deviceName,
                                    path: model.mountPoint,
                                    devicePath: model.devicePath,
                                    mounted: model.mounted,
                                    removable: model.removable
                                }, Qt.point(mapped.x, mapped.y))
                                return
                            }

                            if (model.mounted)
                                root.bookmarkClicked(model.mountPoint)
                            else if (runtimeFeatures.udisksctlAvailable)
                                devices.mount(index)
                            else
                                root.featureHintRequested(runtimeFeatures.installHint("deviceMount"))
                        }
                    }
                }
            }
        }

        // Spacer pushes operations bar to bottom
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // File operations progress bar
        OperationsBar {
            Layout.fillWidth: true
        }
    }
}
