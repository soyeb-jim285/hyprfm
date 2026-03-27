import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

    property string searchQuery: ""
    property string scope: "local"  // "local" | "recursive"
    property bool filterPanelOpen: false

    signal queryChanged(string query)
    signal scopeChanged(string scope)
    signal filterToggled()
    signal searchClosed()
    signal enterPressed()

    color: Theme.surface
    radius: Theme.radiusSmall
    border.width: searchInput.activeFocus ? 1 : 0
    border.color: Theme.accent

    function focusInput() {
        searchInput.forceActiveFocus()
    }

    function clear() {
        searchInput.text = ""
        root.searchQuery = ""
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 4
        spacing: 6

        // Search icon
        IconSearch {
            size: 16
            color: Theme.accent
        }

        // Text input
        TextInput {
            id: searchInput
            Layout.fillWidth: true
            Layout.fillHeight: true
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.text
            selectionColor: Theme.accent
            selectedTextColor: Theme.base
            font.pointSize: Theme.fontNormal
            clip: true

            onTextChanged: {
                root.searchQuery = text
                root.queryChanged(text)
            }

            Keys.onEscapePressed: root.searchClosed()
            Keys.onReturnPressed: root.enterPressed()
            Keys.onEnterPressed: root.enterPressed()

            // Placeholder text
            Text {
                anchors.fill: parent
                verticalAlignment: Text.AlignVCenter
                text: "Search files..."
                color: Theme.muted
                font: searchInput.font
                visible: !searchInput.text && !searchInput.activeFocus
            }
        }

        // Scope toggle
        Rectangle {
            Layout.preferredHeight: 24
            Layout.preferredWidth: scopeRow.implicitWidth + 4
            radius: Theme.radiusSmall
            color: Theme.mantle

            RowLayout {
                id: scopeRow
                anchors.centerIn: parent
                spacing: 0

                Rectangle {
                    Layout.preferredWidth: localLabel.implicitWidth + 16
                    Layout.preferredHeight: 22
                    radius: Theme.radiusSmall
                    color: root.scope === "local" ? Theme.accent : "transparent"

                    Text {
                        id: localLabel
                        anchors.centerIn: parent
                        text: "Current"
                        font.pointSize: Theme.fontSmall
                        color: root.scope === "local" ? Theme.base : Theme.subtext
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.scope = "local"
                            root.scopeChanged("local")
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: recursiveLabel.implicitWidth + 16
                    Layout.preferredHeight: 22
                    radius: Theme.radiusSmall
                    color: root.scope === "recursive" ? Theme.accent : "transparent"

                    Text {
                        id: recursiveLabel
                        anchors.centerIn: parent
                        text: "Recursive"
                        font.pointSize: Theme.fontSmall
                        color: root.scope === "recursive" ? Theme.base : Theme.subtext
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            root.scope = "recursive"
                            root.scopeChanged("recursive")
                        }
                    }
                }
            }
        }

        // Filter button
        HoverRect {
            width: 28; height: 28
            color: root.filterPanelOpen
                ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                : "transparent"
            onClicked: root.filterToggled()

            IconSettings {
                anchors.centerIn: parent
                size: 14
                color: root.filterPanelOpen ? Theme.accent : Theme.subtext
            }
        }

        // Esc hint
        Rectangle {
            Layout.preferredWidth: escLabel.implicitWidth + 12
            Layout.preferredHeight: 20
            radius: 3
            color: Theme.mantle

            Text {
                id: escLabel
                anchors.centerIn: parent
                text: "Esc"
                font.pointSize: Theme.fontSmall - 1
                color: Theme.muted
            }
        }
    }
}
