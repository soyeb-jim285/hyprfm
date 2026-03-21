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

        // Devices section placeholder
        Item {
            Layout.fillWidth: true
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
