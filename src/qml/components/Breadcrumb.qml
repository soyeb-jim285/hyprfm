import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
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

    // Background MouseArea: double-click empty space enters edit mode
    MouseArea {
        anchors.fill: parent
        visible: !root.editMode
        z: -1
        onDoubleClicked: root.startEditing()
    }

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
                    if (!root.path || root.path === "/") return []
                    var homeUrl = Platform.StandardPaths.writableLocation(Platform.StandardPaths.HomeLocation)
                    const homePath = homeUrl.toString().replace("file://", "")
                    const parts = root.path.split("/").filter(p => p !== "")
                    const result = []
                    let accumulated = ""
                    for (const part of parts) {
                        accumulated += "/" + part
                        if (accumulated === homePath) {
                            result.length = 0
                            result.push({ label: "Home", fullPath: accumulated })
                        } else {
                            result.push({ label: part, fullPath: accumulated })
                        }
                    }
                    return result
                }

                delegate: Row {
                    height: segmentsRow.height
                    spacing: 0

                    // Separator (hidden for first segment)
                    Text {
                        text: " / "
                        visible: model.index > 0
                        color: Theme.muted
                        font.pixelSize: Theme.fontNormal
                        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                        height: parent.height
                        verticalAlignment: Text.AlignVCenter
                    }

                    // Segment button
                    Item {
                        height: parent.height
                        width: segRect.width

                    Rectangle {
                        id: segRect
                        height: 24
                        anchors.verticalCenter: parent.verticalCenter
                        width: segLabel.width + Theme.spacing
                        property bool isLast: model.index === segmentsRepeater.count - 1
                        color: !isLast && segHover.containsMouse
                            ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                            : "transparent"
                        radius: Theme.radiusSmall

                        Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                        Text {
                            id: segLabel
                            anchors.centerIn: parent
                            text: modelData.label
                            color: segRect.isLast ? Theme.text : Theme.overlay
                            font.pixelSize: Theme.fontNormal
                            font.weight: Font.Bold
                            verticalAlignment: Text.AlignVCenter
                        }

                        MouseArea {
                            id: segHover
                            anchors.fill: parent
                            hoverEnabled: !segRect.isLast
                            cursorShape: segRect.isLast ? Qt.ArrowCursor : Qt.PointingHandCursor
                            enabled: !segRect.isLast
                            onClicked: root.navigateRequested(modelData.fullPath)
                            onDoubleClicked: root.startEditing()
                        }
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
