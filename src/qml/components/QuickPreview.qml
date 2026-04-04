import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

Item {
    id: root

    property string filePath: ""
    property var directoryFiles: []
    property bool active: false
    property var fileModel: fsModel

    property var fileProps: ({})
    property var textPreview: ({ content: "", truncated: false, isBinary: false, error: "" })
    property var directoryPreview: ({ entries: [], truncated: false, error: "", count: 0 })
    property var pdfPreview: ({ localPath: "", pageCount: 0, error: "" })
    property int pdfPageIndex: 0
    property real pdfWheelAccumulator: 0

    signal closed()
    signal openRequested(string path, bool isDirectory)

    readonly property string fileName: {
        if (fileProps.name)
            return fileProps.name
        if (filePath === "")
            return ""
        var idx = filePath.lastIndexOf("/")
        return idx >= 0 ? filePath.substring(idx + 1) : filePath
    }

    readonly property string fileExtension: {
        var name = fileName
        var dot = name.lastIndexOf(".")
        return dot >= 0 ? name.substring(dot + 1).toLowerCase() : ""
    }

    readonly property bool isDirectory: fileProps.isDir || false
    readonly property bool isArchive: !isDirectory && fileOps.isArchive(filePath)
    readonly property bool isTrashUri: filePath.startsWith("trash:///")
    readonly property bool isRemoteUri: fileOps.isRemotePath(filePath)
    readonly property string _mime: fileProps.mimeType || ""
    readonly property bool isImage: !isRemoteUri && !isDirectory && _mime.startsWith("image/")
    readonly property bool isVideo: !isRemoteUri && !isDirectory && _mime.startsWith("video/")
    readonly property bool isAudio: !isRemoteUri && !isDirectory && _mime.startsWith("audio/")
    readonly property bool isPdf: !isRemoteUri && !isDirectory && _mime === "application/pdf"
    readonly property bool isText: {
        if (isRemoteUri || isDirectory || isPdf || isImage || isVideo || isAudio || isArchive)
            return false
        if (_mime.startsWith("text/"))
            return true
        var textMimes = [
            "application/json", "application/xml", "application/x-yaml",
            "application/toml", "application/x-shellscript",
            "application/javascript", "application/typescript",
            "application/x-tex", "application/x-makefile",
            "application/x-desktop", "application/x-ruby",
            "application/x-perl", "application/x-python"
        ]
        if (textMimes.indexOf(_mime) >= 0)
            return true
        // Fallback: extensionless files and known text extensions not covered by MIME
        if (fileExtension === "")
            return true
        var textExt = ["txt", "md", "json", "yaml", "yml", "toml", "ini", "cfg", "conf",
                       "sh", "bash", "zsh", "fish", "py", "js", "ts", "tsx", "jsx",
                       "css", "html", "htm", "xml", "c", "cpp", "h", "hpp", "rs",
                       "go", "java", "tex", "rb", "lua", "vim", "log", "diff",
                       "patch", "cmake", "qml", "mk", "desktop"]
        return textExt.indexOf(fileExtension) >= 0
    }
    readonly property bool pdfPreviewAvailable: previewService.pdfPreviewAvailable
    readonly property bool videoPreviewAvailable: runtimeFeatures.ffmpegAvailable
    readonly property bool textHighlightAvailable: runtimeFeatures.batAvailable
    readonly property string pdfImageSource: {
        if (!isPdf || !pdfPreview.localPath || pdfPreview.error !== "")
            return ""
        return "image://pdfpreview/" + encodeURIComponent(pdfPreview.localPath)
            + "?page=" + pdfPageIndex
    }
    readonly property string pdfPageLabel: {
        if (!isPdf || pdfPreview.pageCount <= 0)
            return ""
        return "Page " + (pdfPageIndex + 1) + " of " + pdfPreview.pageCount
    }
    readonly property bool hasVisualPreview: isImage || (isVideo && videoPreviewAvailable)
    readonly property string visualSource: {
        if (!hasVisualPreview || filePath === "")
            return ""
        if (isVideo || isTrashUri)
            return "image://thumbnail/" + filePath
        return "file://" + filePath
    }
    readonly property string visualStatusText: {
        if (isVideo)
            return "Video poster preview"
        if (isImage)
            return "Image preview"
        return "Preview"
    }
    readonly property string detailKind: {
        if (isDirectory)
            return "Folder"
        if (isArchive)
            return "Archive"
        if (isAudio)
            return "Audio"
        if (isVideo)
            return "Video"
        if (fileProps.mimeDescription)
            return fileProps.mimeDescription
        if (fileExtension !== "")
            return fileExtension.toUpperCase() + " file"
        return "File"
    }
    readonly property string sidebarPathLabel: fileProps.originalPath || fileProps.parentDir || ""

    visible: active
    focus: active

    component InfoBlock: Column {
        property string label: ""
        property string value: ""
        property bool visibleWhenEmpty: false
        width: parent.width
        spacing: 4
        visible: visibleWhenEmpty || value !== ""

        Text {
            width: parent.width
            text: parent.label
            color: Theme.muted
            font.pointSize: Theme.fontSmall
            font.weight: Font.DemiBold
        }

        Text {
            width: parent.width
            text: parent.value
            color: Theme.text
            font.pointSize: Theme.fontNormal
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }

    function closePreview() {
        active = false
        root.closed()
    }

    function currentIndex() {
        if (directoryFiles.length === 0) return -1
        for (var i = 0; i < directoryFiles.length; i++) {
            if (directoryFiles[i] === filePath) return i
        }
        return -1
    }

    function cycleFile(direction) {
        var files = directoryFiles
        if (files.length === 0) return
        var idx = currentIndex()
        if (idx < 0) idx = 0
        idx = (idx + direction + files.length) % files.length
        filePath = files[idx]
    }

    function refreshPreviewData() {
        if (!active || filePath === "")
            return

        if (fileModel && fileModel.fileProperties)
            fileProps = fileModel.fileProperties(filePath)
        else
            fileProps = ({})

        if (isText)
            textPreview = previewService.loadTextPreview(filePath)
        else
            textPreview = ({ content: "", truncated: false, isBinary: false, error: "" })

        if (isPdf) {
            pdfPreview = previewService.loadPdfPreview(filePath)
            if (pdfPageIndex >= (pdfPreview.pageCount || 0))
                pdfPageIndex = 0
        } else {
            pdfPreview = ({ localPath: "", pageCount: 0, error: "" })
        }

        if (isDirectory)
            directoryPreview = previewService.loadDirectoryPreview(filePath)
        else if (isArchive)
            directoryPreview = previewService.loadArchivePreview(filePath)
        else
            directoryPreview = ({ entries: [], truncated: false, error: "", count: 0 })
    }

    onActiveChanged: {
        if (active) {
            refreshPreviewData()
            Qt.callLater(function() { root.forceActiveFocus() })
        }
    }
    onFilePathChanged: {
        pdfPageIndex = 0
        pdfWheelAccumulator = 0
        refreshPreviewData()
    }
    onFileModelChanged: {
        pdfWheelAccumulator = 0
        refreshPreviewData()
    }

    function changePdfPage(delta) {
        if (!isPdf || pdfPreview.pageCount <= 0)
            return
        pdfPageIndex = Math.max(0, Math.min(pdfPreview.pageCount - 1, pdfPageIndex + delta))
    }

    function handlePdfWheel(wheel) {
        if (!isPdf || pdfPreview.pageCount <= 1)
            return

        var delta = 0
        if (wheel.angleDelta && wheel.angleDelta.y !== 0)
            delta = wheel.angleDelta.y
        else if (wheel.pixelDelta && wheel.pixelDelta.y !== 0)
            delta = wheel.pixelDelta.y * 3

        if (delta === 0)
            return

        pdfWheelAccumulator += delta
        while (pdfWheelAccumulator >= 120) {
            changePdfPage(-1)
            pdfWheelAccumulator -= 120
        }
        while (pdfWheelAccumulator <= -120) {
            changePdfPage(1)
            pdfWheelAccumulator += 120
        }

        wheel.accepted = true
    }

    Keys.onEscapePressed: (event) => {
        event.accepted = true
        closePreview()
    }
    Keys.onLeftPressed: (event) => {
        event.accepted = true
        cycleFile(-1)
    }
    Keys.onRightPressed: (event) => {
        event.accepted = true
        cycleFile(1)
    }
    Keys.onUpPressed: (event) => {
        if (!isPdf)
            return
        event.accepted = true
        changePdfPage(-1)
    }
    Keys.onDownPressed: (event) => {
        if (!isPdf)
            return
        event.accepted = true
        changePdfPage(1)
    }
    Keys.onPressed: (event) => {
        if (!isPdf)
            return
        if (event.key === Qt.Key_PageUp) {
            event.accepted = true
            changePdfPage(-1)
        } else if (event.key === Qt.Key_PageDown) {
            event.accepted = true
            changePdfPage(1)
        }
    }
    Keys.onSpacePressed: (event) => {
        event.accepted = true
        closePreview()
    }
    Keys.onReturnPressed: (event) => {
        event.accepted = true
        root.openRequested(filePath, isDirectory)
        closePreview()
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0.03, 0.04, 0.05, 0.78)
        opacity: overlayOpacity.running ? 0 : 1

        NumberAnimation on opacity {
            id: overlayOpacity
            from: 0
            to: 1
            duration: 160
            easing.type: Easing.OutCubic
            running: root.active
        }

        MouseArea {
            anchors.fill: parent
            onClicked: root.closePreview()
        }
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 1080)
        height: Math.min(parent.height * 0.88, 760)
        radius: Theme.radiusLarge
        color: Qt.rgba(Theme.mantle.r, Theme.mantle.g, Theme.mantle.b, 0.98)
        border.width: 1
        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)

        MouseArea {
            anchors.fill: parent
            onClicked: function() {}
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: root.fileName
                        color: Theme.text
                        font.pointSize: Theme.fontLarge + 2
                        font.bold: true
                        elide: Text.ElideMiddle
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.detailKind
                            + (root.directoryFiles.length > 1
                                ? "  ·  " + (root.currentIndex() + 1) + " of " + root.directoryFiles.length
                                : "")
                            + (root.isPdf && root.pdfPageLabel !== ""
                                ? "  ·  " + root.pdfPageLabel
                                : "")
                        color: Theme.subtext
                        font.pointSize: Theme.fontNormal
                        elide: Text.ElideRight
                    }
                }

                Row {
                    spacing: 8
                    visible: root.isPdf && root.pdfPreview.pageCount > 1

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 17
                        enabled: root.pdfPageIndex > 0
                        opacity: enabled ? 1 : 0.45
                        color: pdfPrevMouse.containsMouse
                            ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                            : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                        IconChevronUp {
                            anchors.centerIn: parent
                            size: 16
                            color: Theme.text
                        }

                        MouseArea {
                            id: pdfPrevMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: parent.enabled
                            onClicked: root.changePdfPage(-1)
                        }
                    }

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 17
                        enabled: root.pdfPageIndex < root.pdfPreview.pageCount - 1
                        opacity: enabled ? 1 : 0.45
                        color: pdfNextMouse.containsMouse
                            ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                            : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                        IconChevronDown {
                            anchors.centerIn: parent
                            size: 16
                            color: Theme.text
                        }

                        MouseArea {
                            id: pdfNextMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: parent.enabled
                            onClicked: root.changePdfPage(1)
                        }
                    }
                }

                Row {
                    spacing: 8
                    visible: root.directoryFiles.length > 1

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 17
                        color: prevMouse.containsMouse
                            ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                            : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                        IconChevronLeft {
                            anchors.centerIn: parent
                            size: 16
                            color: Theme.text
                        }

                        MouseArea {
                            id: prevMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.cycleFile(-1)
                        }
                    }

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 17
                        color: nextMouse.containsMouse
                            ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                            : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                        IconChevronRight {
                            anchors.centerIn: parent
                            size: 16
                            color: Theme.text
                        }

                        MouseArea {
                            id: nextMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.cycleFile(1)
                        }
                    }
                }

                Rectangle {
                    width: 34
                    height: 34
                    radius: 17
                    color: closeMouse.containsMouse
                        ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.18)
                        : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                    IconX {
                        anchors.centerIn: parent
                        size: 16
                        color: Theme.error
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: root.closePreview()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusMedium
                    color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.52)
                    border.width: 1
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                    Item {
                        anchors.fill: parent
                        anchors.margins: 12

                        Image {
                            id: visualPreview
                            anchors.fill: parent
                            visible: root.hasVisualPreview
                            source: root.visualSource
                            sourceSize: Qt.size(width, height)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            smooth: true
                        }

                        Image {
                            id: pdfPreviewImage
                            anchors.fill: parent
                            visible: root.isPdf && root.pdfPreviewAvailable && root.pdfPreview.localPath !== "" && root.pdfPreview.error === ""
                            source: root.pdfImageSource
                            sourceSize: Qt.size(width, height)
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            smooth: true
                        }

                        MouseArea {
                            anchors.fill: parent
                            visible: root.isPdf && root.pdfPreviewAvailable && root.pdfPreview.localPath !== "" && root.pdfPreview.error === ""
                            acceptedButtons: Qt.NoButton
                            onWheel: (wheel) => root.handlePdfWheel(wheel)
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 10
                            visible: (root.hasVisualPreview && visualPreview.status === Image.Error)
                                || (root.isPdf && root.pdfPreviewAvailable && root.pdfPreview.localPath !== "" && pdfPreviewImage.status === Image.Error)

                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 64
                                height: 64
                                source: "image://icon/" + (root.fileProps.iconName || (root.isPdf ? "application-pdf" : "image-x-generic"))
                                sourceSize: Qt.size(width, height)
                                fillMode: Image.PreserveAspectFit
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.isPdf ? "PDF preview could not be loaded" : "Preview could not be loaded"
                                color: Theme.subtext
                                font.pointSize: Theme.fontNormal
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                visible: root.isPdf && root.pdfPreview.error !== ""
                                text: root.pdfPreview.error
                                color: Theme.muted
                                font.pointSize: Theme.fontSmall
                            }
                        }

                        Rectangle {
                            anchors.centerIn: parent
                            visible: (root.hasVisualPreview && visualPreview.status === Image.Loading)
                                || (root.isPdf && root.pdfPreviewAvailable && root.pdfPreview.localPath !== "" && pdfPreviewImage.status === Image.Loading)
                            color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.72)
                            radius: Theme.radiusMedium
                            width: 180
                            height: 44

                            Text {
                                anchors.centerIn: parent
                                text: root.isPdf ? "Rendering PDF..." : root.visualStatusText
                                color: Theme.text
                                font.pointSize: Theme.fontNormal
                            }
                        }

                        Flickable {
                            id: textPreviewFlick
                            anchors.fill: parent
                            visible: root.isText && !root.hasVisualPreview && !root.isPdf
                            clip: true
                            interactive: true
                            boundsMovement: Flickable.StopAtBounds
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: Math.max(width, textArea.contentWidth)
                            contentHeight: Math.max(height, textArea.contentHeight)

                            TextEdit {
                                id: textArea
                                readOnly: true
                                selectByMouse: true
                                textFormat: textPreview.usesBat && textPreview.html !== ""
                                    ? TextEdit.RichText
                                    : TextEdit.PlainText
                                text: textPreview.error !== ""
                                    ? textPreview.error
                                    : (textPreview.isBinary
                                        ? "This file looks binary and cannot be previewed as text."
                                        : (textPreview.usesBat && textPreview.html !== ""
                                            ? textPreview.html
                                            : textPreview.content))
                                color: Theme.text
                                wrapMode: TextEdit.NoWrap
                                font.family: "monospace"
                                font.pointSize: Theme.fontSmall
                            }

                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                        }

                        KineticWheelScroller {
                            anchors.fill: textPreviewFlick
                            visible: textPreviewFlick.visible
                            flickable: textPreviewFlick
                            wheelStep: 28
                            minVelocity: 90
                            maxVelocity: 2600
                            kineticGain: 0.68
                        }

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 8
                            visible: root.isText && textPreview.error === "" && !textPreview.isBinary && !textPreview.usesBat && !root.textHighlightAvailable
                            text: runtimeFeatures.installHint("textHighlight")
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                            horizontalAlignment: Text.AlignHCenter
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 12
                            visible: root.isDirectory || root.isArchive

                            Text {
                                Layout.fillWidth: true
                                text: root.isArchive ? "Archive contents" : (fileProps.contentText || "Folder contents")
                                color: Theme.text
                                font.pointSize: Theme.fontNormal
                                font.bold: true
                            }

                            Text {
                                Layout.fillWidth: true
                                visible: directoryPreview.error !== ""
                                text: directoryPreview.error
                                color: Theme.error
                                font.pointSize: Theme.fontNormal
                                wrapMode: Text.WordWrap
                            }

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                visible: directoryPreview.error === ""

                                ListView {
                                    id: directoryPreviewList
                                    anchors.fill: parent
                                    model: directoryPreview.entries || []
                                    clip: true
                                    spacing: 4

                                    delegate: Text {
                                        width: ListView.view.width
                                        text: modelData
                                        color: Theme.text
                                        font.pointSize: Theme.fontNormal
                                        elide: Text.ElideMiddle
                                    }

                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                }

                                KineticWheelScroller {
                                    anchors.fill: parent
                                    flickable: directoryPreviewList
                                    wheelStep: 28
                                    minVelocity: 90
                                    maxVelocity: 2600
                                    kineticGain: 0.68
                                }
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 12
                            visible: root.isPdf && (!root.pdfPreviewAvailable || pdfPreview.error !== "")

                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 96
                                height: 96
                                source: "image://icon/" + (root.fileProps.iconName || "application-pdf")
                                sourceSize: Qt.size(width, height)
                                fillMode: Image.PreserveAspectFit
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.pdfPreviewAvailable ? "PDF preview is unavailable for this file" : "PDF preview support is unavailable"
                                color: Theme.text
                                font.pointSize: Theme.fontNormal
                                width: 280
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.pdfPreview.error !== ""
                                    ? root.pdfPreview.error
                                    : (root.pdfPreviewAvailable
                                        ? "Press Enter to open the file externally"
                                        : runtimeFeatures.installHint("pdfPreview"))
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                                width: 240
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 12
                            visible: !root.hasVisualPreview && !root.isText && !root.isDirectory && !root.isArchive && !root.isPdf

                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 96
                                height: 96
                                source: "image://icon/" + (root.fileProps.iconName || "text-x-generic")
                                sourceSize: Qt.size(width, height)
                                fillMode: Image.PreserveAspectFit
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.isAudio
                                    ? "Audio preview is not available yet"
                                    : (root.isVideo && !root.videoPreviewAvailable
                                        ? "Video preview support is unavailable"
                                        : "Preview not available")
                                color: Theme.text
                                font.pointSize: Theme.fontNormal
                                width: 280
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.isVideo && !root.videoPreviewAvailable
                                    ? runtimeFeatures.installHint("videoPreview")
                                    : "Press Enter to open externally"
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                width: 280
                                wrapMode: Text.WordWrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 300
                    Layout.minimumWidth: 280
                    Layout.fillHeight: true
                    radius: Theme.radiusMedium
                    color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.7)
                    border.width: 1
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                    Flickable {
                        id: sidebarFlick
                        anchors.fill: parent
                        anchors.margins: 12
                        clip: true
                        interactive: true
                        boundsMovement: Flickable.StopAtBounds
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: width
                        contentHeight: sidebarColumn.implicitHeight

                        Column {
                            id: sidebarColumn
                            width: sidebarFlick.width
                            spacing: 12

                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 72
                                height: 72
                                source: "image://icon/" + (root.fileProps.iconName || "text-x-generic")
                                sourceSize: Qt.size(width, height)
                                fillMode: Image.PreserveAspectFit
                            }

                            InfoBlock { label: "Kind"; value: root.detailKind; visibleWhenEmpty: true }
                            InfoBlock { label: "Pages"; value: root.isPdf && root.pdfPreview.pageCount > 0 ? String(root.pdfPreview.pageCount) : "" }
                            InfoBlock { label: "Size"; value: fileProps.sizeText || "" }
                            InfoBlock { label: root.fileProps.originalPath ? "Original Location" : "Location"; value: root.sidebarPathLabel }
                            InfoBlock { label: "Deleted"; value: fileProps.deleted || "" }
                            InfoBlock { label: "Modified"; value: fileProps.modified || "" }
                            InfoBlock { label: "Contents"; value: fileProps.contentText || "" }

                            Text {
                                width: parent.width
                                visible: root.isText && textPreview.truncated
                                text: "Showing a shortened text preview for quick browsing."
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                width: parent.width
                                visible: (root.isDirectory || root.isArchive) && directoryPreview.truncated
                                text: "Only the first items are shown here."
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                        }

                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    }

                    KineticWheelScroller {
                        anchors.fill: sidebarFlick
                        flickable: sidebarFlick
                        wheelStep: 28
                        minVelocity: 90
                        maxVelocity: 2600
                        kineticGain: 0.68
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: root.directoryFiles.length > 1
                    ? "Use Space or Esc to close, Return to open, and Left/Right to browse nearby items"
                    : "Use Space or Esc to close, and Return to open externally"
                color: Theme.muted
                font.pointSize: Theme.fontSmall
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.WordWrap
            }
        }
    }
}
