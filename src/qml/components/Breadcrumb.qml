import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Qt.labs.platform as Platform
import HyprFM

Item {
    id: root

    property string path: ""
    property var activeTab: null
    property bool isRecentsView: false

    signal navigateRequested(string path)

    // Public method for Ctrl+L shortcut
    function startEditing() {
        editMode = true
        pathInput.text = root.path
        pathInput.selectAll()
        pathInput.forceActiveFocus()
    }

    property bool editMode: false

    // Icon components for breadcrumb context
    Component { id: bcIconHome; IconHome { size: 18; color: Theme.text } }
    Component { id: bcIconClock; IconClock { size: 18; color: Theme.text } }
    Component { id: bcIconTrash; IconTrash { size: 18; color: Theme.text } }
    Component { id: bcIconImage; IconImage { size: 18; color: Theme.text } }
    Component { id: bcIconDownload; IconDownload { size: 18; color: Theme.text } }
    Component { id: bcIconFileText; IconFileText { size: 18; color: Theme.text } }
    Component { id: bcIconMusic; IconMusic { size: 18; color: Theme.text } }
    Component { id: bcIconVideo; IconVideo { size: 18; color: Theme.text } }
    Component { id: bcIconMonitor; IconMonitor { size: 18; color: Theme.text } }
    Component { id: bcIconFolder; IconFolder { size: 18; color: Theme.text } }
    Component { id: bcIconSettings; IconSettings { size: 18; color: Theme.text } }
    Component { id: bcIconRocket; IconRocket { size: 18; color: Theme.text } }

    function iconForLabel(label) {
        const l = label.toLowerCase()
        if (l === "home") return bcIconHome
        if (l === "recents") return bcIconClock
        if (l === "trash") return bcIconTrash
        if (l === "pictures" || l === "photos" || l === "images") return bcIconImage
        if (l === "downloads") return bcIconDownload
        if (l === "documents" || l === "docs") return bcIconFileText
        if (l === "music" || l === "audio") return bcIconMusic
        if (l === "videos" || l === "movies") return bcIconVideo
        if (l === "desktop") return bcIconMonitor
        if (l === ".config" || l === "config" || l === ".local") return bcIconSettings
        if (l === "projects" || l === "code" || l === "dev" || l === "src") return bcIconRocket
        return bcIconFolder
    }

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
            spacing: 4

            // Dynamic context icon
            Loader {
                width: 18; height: 18
                anchors.verticalCenter: parent.verticalCenter
                sourceComponent: {
                    if (segmentsRepeater.count === 0) return bcIconFolder
                    var firstLabel = segmentsRepeater.model[0] ? segmentsRepeater.model[0].label : ""
                    return root.iconForLabel(firstLabel)
                }
            }

            Repeater {
                id: segmentsRepeater
                model: {
                    if (root.isRecentsView) return [{ label: "Recents", fullPath: "" }]
                    if (!root.path || root.path === "/") return []
                    var homeUrl = Platform.StandardPaths.writableLocation(Platform.StandardPaths.HomeLocation)
                    const homePath = homeUrl.toString().replace("file://", "")
                    const trashPath = homePath + "/.local/share/Trash/files"
                    if (root.path === trashPath) return [{ label: "Trash", fullPath: trashPath }]
                    if (root.path.startsWith(trashPath + "/")) {
                        const sub = root.path.substring(trashPath.length + 1).split("/")
                        const result = [{ label: "Trash", fullPath: trashPath }]
                        let acc = trashPath
                        for (const part of sub) {
                            acc += "/" + part
                            result.push({ label: part, fullPath: acc })
                        }
                        return result
                    }
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

                        property bool isLast: model.index === segmentsRepeater.count - 1

                        HoverRect {
                            id: segRect
                            height: 24
                            anchors.verticalCenter: parent.verticalCenter
                            width: segLabel.width + Theme.spacing
                            hoverEnabled: !parent.isLast
                            onClicked: root.navigateRequested(modelData.fullPath)
                            onDoubleClicked: root.startEditing()

                            Text {
                                id: segLabel
                                anchors.centerIn: parent
                                text: modelData.label
                                color: parent.parent.isLast ? Theme.text : Theme.overlay
                                font.pixelSize: Theme.fontNormal
                                font.weight: Font.Bold
                                verticalAlignment: Text.AlignVCenter
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
