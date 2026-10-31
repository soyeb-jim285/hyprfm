import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

Item {
    id: root
    objectName: "breadcrumb"
    Accessible.role: Accessible.Pane
    Accessible.name: "Breadcrumb navigation: " + root.path

    property string path: ""
    property var activeTab: null
    property bool isRecentsView: false

    signal navigateRequested(string path)

    function normalizedInputPath(text) {
        var trimmed = text.trim()
        if (trimmed === "~")
            return fsModel.homePath()
        if (trimmed.startsWith("~/"))
            return fsModel.homePath() + trimmed.substring(1)
        // Anything else that is not a URI ("sftp://", "trash:///") is a folder
        // in the current one; "v1:old" is a folder name, not a scheme.
        if (trimmed !== "" && !trimmed.startsWith("/")
                && !/^[a-zA-Z][a-zA-Z0-9+.-]*:\/\//.test(trimmed))
            return (root.isRecentsView ? fsModel.homePath() : root.path).replace(/\/$/, "") + "/" + trimmed
        return trimmed
    }

    function clearSuggestions() {
        suggestionItems = []
        suggestionIndex = -1
    }

    function updateSuggestions() {
        if (!root.editMode) {
            clearSuggestions()
            return
        }

        var query = pathInput.text.trim()
        if (query === "") {
            clearSuggestions()
            return
        }

        // pathSuggestions keeps a typed "~" in what it offers, so only a
        // relative path is resolved here.
        suggestionItems = fsModel.pathSuggestions(query.startsWith("~") ? query : normalizedInputPath(query),
                                                  query.endsWith("/") ? -1 : 8)
        // Return opens what was typed until a suggestion is explicitly chosen.
        suggestionIndex = -1
    }

    function commitNavigation(targetPath) {
        var destination = normalizedInputPath(targetPath)
        if (destination === "")
            return

        root.editMode = false
        clearSuggestions()
        root.navigateRequested(destination)
    }

    function applySuggestion(index, navigateNow) {
        if (index < 0 || index >= suggestionItems.length)
            return

        var suggestion = suggestionItems[index]
        // Setting the text rebuilds suggestionItems, so the old index means
        // nothing now: Return opens the completed text.
        pathInput.text = suggestion.displayPath || suggestion.path || ""
        pathInput.cursorPosition = pathInput.text.length

        if (navigateNow)
            commitNavigation(suggestion.path || pathInput.text)
    }

    // Public method for Ctrl+L shortcut
    function startEditing() {
        editMode = true
        pathInput.text = root.path
        pathInput.selectAll()
        pathInput.forceActiveFocus()
        updateSuggestions()
    }

    property bool editMode: false
    property var suggestionItems: []
    property int suggestionIndex: -1

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
    clip: false

    // Background MouseArea: right click anywhere on the bar (config-gated)
    // enters path edit mode with all text selected.
    MouseArea {
        anchors.fill: parent
        visible: !root.editMode && config.rightClickToEditPath
        z: -1
        acceptedButtons: Qt.RightButton
        onPressed: root.startEditing()
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

        // Long paths overflow: keep the current folder, the end, in view.
        function showEnd() { contentX = Math.max(0, contentWidth - width) }
        onContentWidthChanged: showEnd()
        onWidthChanged: showEnd()

        // Left click on empty space edits the path. It sits inside the
        // Flickable, which would otherwise take the click.
        MouseArea {
            width: Math.max(segmentsFlickable.contentWidth, segmentsFlickable.width)
            height: segmentsFlickable.height
            cursorShape: Qt.IBeamCursor
            onClicked: root.startEditing()
        }

        Row {
            id: segmentsRow
            height: parent.height
            spacing: 4

            // Dynamic context icon
            Loader {
                width: 18; height: 18
                anchors.verticalCenter: parent.verticalCenter
                sourceComponent: {
                    if (fileOps.isRemotePath(root.path)) return bcIconMonitor
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

                    return fileOps.breadcrumbSegments(root.path)
                }

                delegate: Row {
                    height: segmentsRow.height
                    spacing: 0

                    // Separator (hidden for first segment)
                    Text {
                        text: " / "
                        visible: model.index > 0
                        color: Theme.muted
                        font.pointSize: Theme.fontNormal
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
                            // The current folder has nowhere to go: clicking it edits the path.
                            cursorShape: parent.isLast ? Qt.IBeamCursor : Qt.PointingHandCursor
                            onClicked: parent.isLast ? root.startEditing() : root.navigateRequested(modelData.fullPath)
                            onDoubleClicked: root.startEditing()

                            Text {
                                id: segLabel
                                anchors.centerIn: parent
                                text: modelData.label
                                color: parent.parent.isLast ? Theme.text : Theme.overlay
                                font.pointSize: Theme.fontNormal
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
        id: inputFrame
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        height: 28
        visible: root.editMode
        color: Theme.surface
        radius: Theme.radiusSmall
        border.color: Theme.accent
        border.width: 1

        TextInput {
            id: pathInput
            objectName: "pathInput"
            Accessible.role: Accessible.EditableText
            Accessible.name: "Folder path"
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.text
            font.pointSize: Theme.fontNormal
            selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
            clip: true

            Keys.onDownPressed: (event) => {
                if (root.suggestionItems.length === 0)
                    return
                root.suggestionIndex = Math.min(root.suggestionItems.length - 1, Math.max(0, root.suggestionIndex + 1))
                suggestionList.positionViewAtIndex(root.suggestionIndex, ListView.Contain)
                event.accepted = true
            }

            Keys.onUpPressed: (event) => {
                if (root.suggestionItems.length === 0)
                    return
                root.suggestionIndex = Math.max(0, root.suggestionIndex - 1)
                suggestionList.positionViewAtIndex(root.suggestionIndex, ListView.Contain)
                event.accepted = true
            }

            Keys.onTabPressed: (event) => {
                if (root.suggestionItems.length === 0)
                    return
                root.applySuggestion(root.suggestionIndex >= 0 ? root.suggestionIndex : 0, false)
                event.accepted = true
            }

            Keys.onReturnPressed: {
                if (root.suggestionItems.length > 0 && root.suggestionIndex >= 0)
                    root.commitNavigation(root.suggestionItems[root.suggestionIndex].path || pathInput.text)
                else
                    root.commitNavigation(pathInput.text)
            }
            Keys.onEscapePressed: {
                root.editMode = false
                root.clearSuggestions()
            }

            onActiveFocusChanged: {
                if (!activeFocus && root.editMode)
                    blurCloseTimer.restart()
            }

            onTextChanged: root.updateSuggestions()
        }

        Rectangle {
            id: suggestionsPanel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.bottom
            anchors.topMargin: 6
            visible: root.editMode && root.suggestionItems.length > 0
            height: Math.min(suggestionList.contentHeight + 8, 248)
            z: 20
            radius: Theme.radiusMedium
            color: Theme.containerColor(Theme.mantle, 0.98)
            border.width: 1
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
            clip: true

            MouseArea {
                id: suggestionsHoverArea
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                hoverEnabled: true
                z: -1
            }

            ListView {
                id: suggestionList
                anchors.fill: parent
                anchors.margins: 4
                model: root.suggestionItems
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                spacing: 2

                delegate: Rectangle {
                    required property int index
                    required property var modelData

                    width: suggestionList.width
                    height: 34
                    radius: Theme.radiusSmall
                    color: root.suggestionIndex === index
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                        : mouseArea.containsMouse
                            ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                            : "transparent"

                    Behavior on color { ColorAnimation { duration: Theme.animDurationFast } }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        IconFolder {
                            anchors.verticalCenter: parent.verticalCenter
                            size: 16
                            color: root.suggestionIndex === index ? Theme.accent : Theme.subtext
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 24
                            textFormat: Text.PlainText
                            text: modelData.displayPath || modelData.path || ""
                            color: Theme.text
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideMiddle
                        }
                    }

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.applySuggestion(index, true)
                    }
                }
            }
        }

        Timer {
            id: blurCloseTimer
            interval: 0
            onTriggered: {
                if (!pathInput.activeFocus && !suggestionsHoverArea.containsMouse) {
                    root.editMode = false
                    root.clearSuggestions()
                }
            }
        }
    }
}
