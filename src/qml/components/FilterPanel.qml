import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

    property var activeTypes: []  // array of active type values
    property string activeDateFilter: ""
    property string activeSizeFilter: ""

    signal typeFilterChanged(string filter)
    signal dateFilterChanged(string filter)
    signal sizeFilterChanged(string filter)
    signal clearAllFilters()

    function isTypeActive(value) {
        return activeTypes.indexOf(value) >= 0
    }

    function toggleType(value) {
        var types = activeTypes.slice()
        var idx = types.indexOf(value)
        if (idx >= 0)
            types.splice(idx, 1)
        else
            types.push(value)
        activeTypes = types
        typeFilterChanged(types.join(","))
    }

    color: Theme.mantle
    implicitHeight: content.implicitHeight + 16

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            spacing: 16

            // -- Type filter --
            ColumnLayout {
                spacing: 4

                Text {
                    text: "Type"
                    font.pointSize: Theme.fontSmall
                    color: Theme.muted
                    font.weight: Font.Medium
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: [
                            { label: "Folders", value: "folders" },
                            { label: "Documents", value: "documents" },
                            { label: "Images", value: "images" },
                            { label: "Audio", value: "audio" },
                            { label: "Video", value: "video" },
                            { label: "Code", value: "code" },
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: chipText.implicitWidth + 16
                            height: 26
                            radius: 13
                            color: root.isTypeActive(modelData.value)
                                ? Theme.accent
                                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                            Behavior on color { ColorAnimation { duration: 150 } }

                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pointSize: Theme.fontSmall
                                color: root.isTypeActive(modelData.value) ? Theme.base : Theme.subtext
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.toggleType(modelData.value)
                            }
                        }
                    }
                }
            }

            // -- Separator --
            Rectangle { width: 1; Layout.fillHeight: true; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) }

            // -- Date filter --
            ColumnLayout {
                spacing: 4

                Text {
                    text: "Modified"
                    font.pointSize: Theme.fontSmall
                    color: Theme.muted
                    font.weight: Font.Medium
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: [
                            { label: "Today", value: "today" },
                            { label: "This week", value: "week" },
                            { label: "This month", value: "month" },
                            { label: "This year", value: "year" },
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: dateText.implicitWidth + 16
                            height: 26
                            radius: 13
                            color: root.activeDateFilter === modelData.value
                                ? Theme.accent
                                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                            Behavior on color { ColorAnimation { duration: 150 } }

                            Text {
                                id: dateText
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pointSize: Theme.fontSmall
                                color: root.activeDateFilter === modelData.value ? Theme.base : Theme.subtext
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.activeDateFilter === modelData.value) {
                                        root.activeDateFilter = ""
                                        root.dateFilterChanged("")
                                    } else {
                                        root.activeDateFilter = modelData.value
                                        root.dateFilterChanged(modelData.value)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // -- Separator --
            Rectangle { width: 1; Layout.fillHeight: true; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) }

            // -- Size filter --
            ColumnLayout {
                spacing: 4

                Text {
                    text: "Size"
                    font.pointSize: Theme.fontSmall
                    color: Theme.muted
                    font.weight: Font.Medium
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 4

                    Repeater {
                        model: [
                            { label: "< 10KB", value: "tiny" },
                            { label: "< 1MB", value: "small" },
                            { label: "< 100MB", value: "medium" },
                            { label: "< 1GB", value: "large" },
                            { label: "> 1GB", value: "huge" },
                        ]

                        delegate: Rectangle {
                            required property var modelData
                            width: sizeText.implicitWidth + 16
                            height: 26
                            radius: 13
                            color: root.activeSizeFilter === modelData.value
                                ? Theme.accent
                                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                            Behavior on color { ColorAnimation { duration: 150 } }

                            Text {
                                id: sizeText
                                anchors.centerIn: parent
                                text: modelData.label
                                font.pointSize: Theme.fontSmall
                                color: root.activeSizeFilter === modelData.value ? Theme.base : Theme.subtext
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.activeSizeFilter === modelData.value) {
                                        root.activeSizeFilter = ""
                                        root.sizeFilterChanged("")
                                    } else {
                                        root.activeSizeFilter = modelData.value
                                        root.sizeFilterChanged(modelData.value)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // -- Spacer + Clear --
            Item { Layout.fillWidth: true }

            Rectangle {
                Layout.preferredWidth: clearText.implicitWidth + 16
                Layout.preferredHeight: 26
                Layout.alignment: Qt.AlignBottom
                radius: 13
                color: clearHover.hovered
                    ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.15)
                    : "transparent"
                visible: root.activeTypes.length > 0 || root.activeDateFilter !== "" || root.activeSizeFilter !== ""

                Text {
                    id: clearText
                    anchors.centerIn: parent
                    text: "Clear all"
                    font.pointSize: Theme.fontSmall
                    color: Theme.error
                }

                HoverHandler { id: clearHover; cursorShape: Qt.PointingHandCursor }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.activeTypes = []
                        root.activeDateFilter = ""
                        root.activeSizeFilter = ""
                        root.clearAllFilters()
                    }
                }
            }
        }
    }
}
