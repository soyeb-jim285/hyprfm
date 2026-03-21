import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

    property string currentPath: ""
    signal bookmarkClicked(string path)

    color: Theme.mantle

    // Map common folder names to emoji icons
    function folderIcon(name) {
        const lower = name.toLowerCase()
        if (lower === "home" || lower === "~") return "🏠"
        if (lower === "documents" || lower === "docs") return "📄"
        if (lower === "downloads") return "⬇️"
        if (lower === "pictures" || lower === "photos" || lower === "images") return "🖼️"
        if (lower === "music" || lower === "audio") return "🎵"
        if (lower === "videos" || lower === "movies") return "🎬"
        if (lower === "desktop") return "🖥️"
        if (lower === "trash") return "🗑️"
        if (lower === "projects" || lower === "code" || lower === "dev") return "💻"
        if (lower === "games") return "🎮"
        if (lower === "public") return "🌐"
        if (lower === "templates") return "📋"
        return "📁"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Bookmarks section
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top

                // Section header
                Item {
                    width: parent.width
                    height: 32

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: "BOOKMARKS"
                        color: Theme.muted
                        font.pixelSize: Theme.fontSmall - 1
                        font.letterSpacing: 1.0
                    }
                }

                // Bookmark entries
                Repeater {
                    model: bookmarks

                    delegate: Rectangle {
                        id: bookmarkDelegate
                        width: parent.width
                        height: 32
                        color: {
                            if (model.path === root.currentPath) return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                            if (hoverArea.containsMouse) return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                            return "transparent"
                        }
                        radius: Theme.radiusSmall

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacing
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacing
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacing

                            Text {
                                text: root.folderIcon(model.name)
                                font.pixelSize: Theme.fontNormal
                                verticalAlignment: Text.AlignVCenter
                            }

                            Text {
                                text: model.name
                                color: model.path === root.currentPath ? Theme.accent : Theme.text
                                font.pixelSize: Theme.fontNormal
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                width: parent.width - 32 - Theme.spacing
                            }
                        }

                        MouseArea {
                            id: hoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.bookmarkClicked(model.path)
                        }
                    }
                }
            }
        }

        // Devices section
        Item {
            Layout.fillWidth: true
            implicitHeight: devicesColumn.implicitHeight

            Column {
                id: devicesColumn
                anchors.left: parent.left
                anchors.right: parent.right

                // Section header
                Item {
                    width: parent.width
                    height: 32

                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: "DEVICES"
                        color: Theme.muted
                        font.pixelSize: Theme.fontSmall - 1
                        font.letterSpacing: 1.0
                    }
                }

                // Device entries
                Repeater {
                    model: devices

                    delegate: Rectangle {
                        id: deviceDelegate
                        width: parent.width
                        height: deviceContent.implicitHeight + 8
                        color: deviceHoverArea.containsMouse
                            ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                            : "transparent"
                        radius: Theme.radiusSmall

                        Column {
                            id: deviceContent
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.spacing
                            anchors.rightMargin: Theme.spacing
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4

                            // Device row: icon + name + eject button
                            Item {
                                width: parent.width
                                height: 24

                                Row {
                                    anchors.left: parent.left
                                    anchors.right: ejectBtn.visible ? ejectBtn.left : parent.right
                                    anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.spacing

                                    Text {
                                        text: model.removable ? "🔌" : "💿"
                                        font.pixelSize: Theme.fontNormal
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    Text {
                                        text: model.deviceName
                                        color: Theme.text
                                        font.pixelSize: Theme.fontNormal
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        width: parent.width - 24 - Theme.spacing
                                    }
                                }

                                Text {
                                    id: ejectBtn
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: model.removable
                                    text: "⏏"
                                    font.pixelSize: Theme.fontNormal
                                    color: ejectHover.containsMouse ? Theme.error : Theme.muted

                                    MouseArea {
                                        id: ejectHover
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: devices.unmount(index)
                                    }
                                }
                            }

                            // Storage usage bar
                            Rectangle {
                                width: parent.width
                                height: 4
                                radius: 2
                                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)

                                Rectangle {
                                    width: parent.width * Math.min(model.usagePercent / 100.0, 1.0)
                                    height: parent.height
                                    radius: parent.radius
                                    color: model.usagePercent >= 90
                                        ? Theme.error
                                        : model.usagePercent >= 75
                                            ? Theme.warning
                                            : Theme.accent
                                }
                            }

                            // "X free of Y" text
                            Text {
                                width: parent.width
                                text: {
                                    function fmt(b) {
                                        if (b <= 0) return "0 B"
                                        const units = ["B","KB","MB","GB","TB"]
                                        let i = 0
                                        let v = b
                                        while (v >= 1024 && i < units.length - 1) { v /= 1024; i++ }
                                        return v.toFixed(1) + " " + units[i]
                                    }
                                    return fmt(model.freeSpace) + " free of " + fmt(model.totalSize)
                                }
                                color: Theme.muted
                                font.pixelSize: Theme.fontSmall - 1
                                elide: Text.ElideRight
                            }
                        }

                        MouseArea {
                            id: deviceHoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            // Navigate to mount point on click
                            onClicked: root.bookmarkClicked(model.mountPoint)
                        }
                    }
                }
            }
        }

        // Spacer before operations bar
        Item {
            Layout.fillWidth: true
            height: Theme.spacing
        }

        // File operations progress bar
        OperationsBar {
            Layout.fillWidth: true
        }
    }
}
