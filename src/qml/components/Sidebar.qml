import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import HyprFM

Rectangle {
    id: root

    property string currentPath: ""
    property bool isRecentsView: false
    signal bookmarkClicked(string path)
    signal recentsClicked()
    signal collapseClicked()

    color: Theme.mantle
    clip: false

    Component { id: iconHome; IconHome { size: 18; color: Theme.subtext } }
    Component { id: iconClock; IconClock { size: 18; color: Theme.subtext } }
    Component { id: iconTrash; IconTrash { size: 18; color: Theme.subtext } }
    Component { id: iconImage; IconImage { size: 18; color: Theme.subtext } }
    Component { id: iconDownload; IconDownload { size: 18; color: Theme.subtext } }
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
                    ListElement { name: "Pictures"; iconType: "image" }
                    ListElement { name: "Downloads"; iconType: "download" }
                }

                delegate: Rectangle {
                    id: quickAccessDelegate

                    readonly property string resolvedPath: {
                        const home = fsModel.homePath()
                        if (model.name === "Home") return home
                        if (model.name === "Recents") return ""
                        if (model.name === "Trash") return home + "/.local/share/Trash/files"
                        if (model.name === "Pictures") return home + "/Pictures"
                        if (model.name === "Downloads") return home + "/Downloads"
                        return ""
                    }

                    width: parent.width - Theme.spacing
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: 32
                    readonly property bool isActive: {
                        if (model.name === "Recents") return root.isRecentsView
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
                        onClicked: {
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

        // Bookmarks section — drag folders here to add
        Item {
            id: bookmarksSection
            Layout.fillWidth: true
            implicitHeight: bookmarksList.implicitHeight + (dropIndicator.visible ? 4 : 0)

            property int dropIndex: -1

            // Full-area drop zone
            DropArea {
                id: bookmarkDropArea
                anchors.fill: parent
                keys: ["text/uri-list"]

                onPositionChanged: (drag) => {
                    // Calculate insertion index from y position
                    var localY = drag.y
                    var idx = Math.round(localY / 32)
                    bookmarksSection.dropIndex = Math.max(0, Math.min(idx, bookmarks.count))
                }
                onExited: bookmarksSection.dropIndex = -1

                onDropped: (drop) => {
                    var insertAt = bookmarksSection.dropIndex
                    bookmarksSection.dropIndex = -1
                    var urls = drop.urls || []
                    if (urls.length === 0 && drop.text)
                        urls = drop.text.split("\n").filter(u => u.startsWith("file://"))
                    for (var i = 0; i < urls.length; i++) {
                        var path = urls[i].toString().replace("file://", "").replace(/\/$/, "")
                        path = decodeURIComponent(path)
                        if (path !== "")
                            bookmarks.insertBookmark(path, insertAt >= 0 ? insertAt : bookmarks.count)
                    }
                    drop.accept()
                }
            }

            // Insertion indicator line
            Rectangle {
                id: dropIndicator
                visible: bookmarksSection.dropIndex >= 0
                width: parent.width - Theme.spacing * 2
                anchors.horizontalCenter: parent.horizontalCenter
                height: 2
                radius: 1
                color: Theme.accent
                y: bookmarksSection.dropIndex * 32

                Behavior on y {
                    NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                }
            }

            Column {
                id: bookmarksList
                width: parent.width

                add: Transition {
                    ParallelAnimation {
                        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.OutCubic }
                        NumberAnimation { property: "scale"; from: 0.9; to: 1; duration: 250; easing.type: Easing.OutBack; easing.overshoot: 0.6 }
                    }
                }
                move: Transition {
                    NumberAnimation { properties: "x,y"; duration: 200; easing.type: Easing.InOutCubic }
                }

                Repeater {
                    model: bookmarks

                    delegate: Rectangle {
                        id: bmDelegate
                        width: bookmarksList.width - Theme.spacing
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: 32

                        readonly property bool isActive:
                            !root.isRecentsView && model.path === root.currentPath

                        color: {
                            if (isActive) return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                            if (bmHoverArea.containsMouse || removeMa.containsMouse)
                                return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                            return "transparent"
                        }
                        radius: Theme.radiusSmall
                        Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                        // Background hover area (declared first = lowest z)
                        MouseArea {
                            id: bmHoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.LeftButton
                            onClicked: root.bookmarkClicked(model.path)
                        }

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacing
                            anchors.right: removeBtnArea.left
                            anchors.rightMargin: 4
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

                        // Remove button — on top of hover area
                        Item {
                            id: removeBtnArea
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacing - 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 22; height: 22
                            visible: bmHoverArea.containsMouse || removeMa.containsMouse

                            IconX {
                                anchors.centerIn: parent
                                size: 14
                                color: removeMa.containsMouse ? Theme.text : Theme.muted
                            }

                            MouseArea {
                                id: removeMa
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: bookmarks.removeBookmark(index)
                            }
                        }
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
                        onClicked: {
                            if (model.mounted)
                                root.bookmarkClicked(model.mountPoint)
                            else
                                devices.mount(index)
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
