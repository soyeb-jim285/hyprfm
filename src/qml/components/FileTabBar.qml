import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root
    Accessible.role: Accessible.PageTabList
    Accessible.name: "File tabs"

    height: 40
    color: Theme.mantle

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Scrollable tab list
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Row {
                id: tabRow
                height: parent.height
                spacing: 1

                Repeater {
                    id: tabRepeater
                    model: tabModel

                    delegate: Rectangle {
                        id: tabDelegate

                        required property int index
                        required property var model

                        width: Math.min(200, Math.max(100, tabRow.parent.width / Math.max(tabModel.count, 1) - 1))
                        height: root.height
                        color: tabDelegate.index === tabModel.activeIndex ? Theme.base
                             : tabDropArea.containsDrag ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.15)
                             : "transparent"
                        radius: Theme.radiusSmall

                        // Bottom fill to blend with content below when active
                        Rectangle {
                            visible: tabDelegate.index === tabModel.activeIndex
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: Theme.radiusSmall
                            color: Theme.base
                        }

                        // ── Drop area on tab header ───────────────────────
                        DropArea {
                            id: tabDropArea
                            anchors.fill: parent
                            keys: ["text/uri-list"]

                            onDropped: (drop) => {
                                // model.path is the tab's current directory
                                var destPath = tabDelegate.model.path
                                if (!destPath) return

                                var urls = drop.urls
                                var paths = []
                                for (var i = 0; i < urls.length; i++) {
                                    var s = urls[i].toString()
                                    paths.push(s.startsWith("file://") ? decodeURIComponent(s.substring(7)) : s)
                                }
                                if (paths.length === 0) return
                                var usesRemotePath = fileOps.isRemotePath(destPath)
                                for (var j = 0; j < paths.length; ++j) {
                                    if (fileOps.isRemotePath(paths[j])) {
                                        usesRemotePath = true
                                        break
                                    }
                                }
                                if (drop.proposedAction === Qt.MoveAction) {
                                    if (usesRemotePath)
                                        fileOps.moveFiles(paths, destPath)
                                    else
                                        undoManager.moveFiles(paths, destPath)
                                } else {
                                    if (usesRemotePath)
                                        fileOps.copyFiles(paths, destPath)
                                    else
                                        undoManager.copyFiles(paths, destPath)
                                }
                                drop.acceptProposedAction()
                            }
                        }

                        // Tab switching — TapHandler doesn't block child MouseAreas
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: tabModel.activeIndex = tabDelegate.index
                        }

                        // Middle-click to close
                        TapHandler {
                            acceptedButtons: Qt.MiddleButton
                            onTapped: tabModel.closeTab(tabDelegate.index)
                        }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacing
                            anchors.rightMargin: closeBtn.visible ? closeBtn.width + 8 : 4
                            spacing: 4

                            Image {
                                width: 16
                                height: 16
                                source: "image://icon/folder"
                                sourceSize: Qt.size(16, 16)
                                anchors.verticalCenter: parent.verticalCenter
                            }

                            Text {
                                width: parent.width - 20 - parent.spacing
                                text: model.title || "New Tab"
                                color: tabDelegate.index === tabModel.activeIndex ? Theme.text : Theme.subtext
                                font.pointSize: Theme.fontNormal
                                elide: Text.ElideRight
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }

                        // Close button
                        Rectangle {
                            id: closeBtn
                            width: 22
                            height: 22
                            radius: 11
                            anchors.right: parent.right
                            anchors.rightMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            color: closeHover.hovered ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.8) : "transparent"
                            visible: tabModel.count > 1

                            IconX {
                                anchors.centerIn: parent
                                size: 12
                                color: closeHover.hovered ? Theme.base : Theme.muted
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: tabModel.closeTab(tabDelegate.index)
                            }

                            HoverHandler {
                                id: closeHover
                                cursorShape: Qt.PointingHandCursor
                            }
                        }
                    }
                }
            }
        }

        // Add tab button
        HoverRect {
            id: addBtn
            width: 40
            height: root.height
            onClicked: tabModel.addTab()

            Text {
                anchors.centerIn: parent
                text: "+"
                color: addBtn.hovered ? Theme.text : Theme.subtext
                font.pointSize: Theme.fontLarge
                verticalAlignment: Text.AlignVCenter
            }
        }
    }
}
