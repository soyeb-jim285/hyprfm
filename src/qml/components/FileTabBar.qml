import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

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
                        width: Math.min(200, Math.max(100, tabRow.parent.width / Math.max(tabModel.count, 1) - 1))
                        height: root.height
                        color: index === tabModel.activeIndex ? Theme.base : "transparent"
                        radius: Theme.radiusSmall

                        // Bottom fill to blend with content below when active
                        Rectangle {
                            visible: index === tabModel.activeIndex
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: Theme.radiusSmall
                            color: Theme.base
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacing
                            anchors.rightMargin: 4
                            spacing: 4

                            // Folder icon
                            Text {
                                text: "📁"
                                font.pixelSize: Theme.fontSmall
                                verticalAlignment: Text.AlignVCenter
                            }

                            // Tab title
                            Text {
                                Layout.fillWidth: true
                                text: model.title || "New Tab"
                                color: index === tabModel.activeIndex ? Theme.text : Theme.subtext
                                font.pixelSize: Theme.fontNormal
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            // Close button
                            Rectangle {
                                id: closeBtn
                                width: 18
                                height: 18
                                radius: 9
                                color: closeHover.containsMouse ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.8) : "transparent"
                                visible: tabModel.count > 1 || closeHover.containsMouse

                                Text {
                                    anchors.centerIn: parent
                                    text: "✕"
                                    font.pixelSize: Theme.fontSmall - 1
                                    color: closeHover.containsMouse ? Theme.base : Theme.muted
                                }

                                MouseArea {
                                    id: closeHover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        mouse.accepted = true
                                        tabModel.closeTab(index)
                                    }
                                }
                            }
                        }

                        // Tab click and middle-click
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                            onClicked: {
                                if (mouse.button === Qt.MiddleButton) {
                                    tabModel.closeTab(index)
                                } else {
                                    tabModel.activeIndex = index
                                }
                            }
                            // Don't capture close button area
                            onPressed: {
                                if (mouse.button === Qt.LeftButton) {
                                    tabModel.activeIndex = index
                                }
                            }
                        }
                    }
                }
            }
        }

        // Add tab button
        Rectangle {
            width: 40
            height: root.height
            color: addHover.containsMouse ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent"

            Text {
                anchors.centerIn: parent
                text: "+"
                color: addHover.containsMouse ? Theme.text : Theme.subtext
                font.pixelSize: Theme.fontLarge
                verticalAlignment: Text.AlignVCenter
            }

            MouseArea {
                id: addHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: tabModel.addTab()
            }
        }
    }
}
