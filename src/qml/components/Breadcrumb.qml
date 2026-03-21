import QtQuick
import QtQuick.Controls
import HyprFM

Item {
    id: root

    property string path: ""
    property var activeTab: null

    signal navigateRequested(string path)

    // Public method for Ctrl+L shortcut
    function startEditing() {
        editMode = true
        pathInput.text = root.path
        pathInput.selectAll()
        pathInput.forceActiveFocus()
    }

    property bool editMode: false

    height: 28
    clip: true

    // Clickable path segments (display mode)
    Flickable {
        id: segmentsFlickable
        anchors.fill: parent
        visible: !root.editMode
        contentWidth: segmentsRow.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        clip: true

        Row {
            id: segmentsRow
            height: parent.height
            spacing: 0

            Repeater {
                id: segmentsRepeater
                model: {
                    if (!root.path || root.path === "/") return ["/"]
                    const parts = root.path.split("/").filter(p => p !== "")
                    const result = ["/"]
                    let accumulated = ""
                    for (const part of parts) {
                        accumulated += "/" + part
                        result.push({ label: part, fullPath: accumulated })
                    }
                    return result
                }

                delegate: Row {
                    height: segmentsRow.height
                    spacing: 0

                    // Separator (except before root)
                    Text {
                        visible: index > 0
                        text: " / "
                        color: Theme.muted
                        font.pixelSize: Theme.fontNormal
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                    }

                    // Segment button
                    Rectangle {
                        height: parent.height
                        width: segLabel.width + Theme.spacing
                        color: segHover.containsMouse
                            ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                            : "transparent"
                        radius: Theme.radiusSmall

                        Text {
                            id: segLabel
                            anchors.centerIn: parent
                            text: typeof modelData === "string" ? modelData : modelData.label
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                            verticalAlignment: Text.AlignVCenter
                        }

                        MouseArea {
                            id: segHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                const target = typeof modelData === "string" ? "/" : modelData.fullPath
                                root.navigateRequested(target)
                            }
                            onDoubleClicked: root.startEditing()
                        }
                    }
                }
            }
        }
    }

    // Edit mode text input
    Rectangle {
        anchors.fill: parent
        visible: root.editMode
        color: Theme.surface
        radius: Theme.radiusSmall
        border.color: Theme.accent
        border.width: 1

        TextInput {
            id: pathInput
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)

            Keys.onReturnPressed: {
                root.editMode = false
                root.navigateRequested(pathInput.text)
            }
            Keys.onEscapePressed: {
                root.editMode = false
            }

            onActiveFocusChanged: {
                if (!activeFocus && root.editMode) {
                    root.editMode = false
                }
            }
        }
    }
}
