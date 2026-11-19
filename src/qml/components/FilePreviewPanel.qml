import QtQuick
import QtQuick.Controls
import HyprFM
import Quill as Q

Item {
    id: previewColumn
    objectName: "filePreviewPanel"
    clip: true

    required property var directoryModel
    property string requester: "panel"

    property string previewFilePath: ""
    property bool previewIsDir: false

    // The page we are navigating away from, held on screen until the
    // incoming one is Ready. An Image drops its texture the moment
    // source changes, so without this every page turn flashes empty
    // -- even on a cache hit, which still resolves a frame or two
    // later because the provider is asynchronous.
    property string pdfHoldSource: ""

    // Rich preview data (like QuickPreview)
    property var fileProps: ({})
    readonly property var textPreview: previewLoader.textPreview
    readonly property var directoryPreview: previewLoader.directoryPreview
    readonly property var pdfPreview: previewLoader.pdfPreview
    readonly property var fileMetadata: previewLoader.fileMetadata
    readonly property bool previewLoading: previewLoader.loading
    // Fonts stay local: QFontDatabase is GUI-thread-only.
    property var fontPreview: ({ family: "", styleName: "", weight: 400, italic: false, valid: false, error: "" })

    PreviewLoader {
        id: previewLoader
        requester: previewColumn.requester
        path: previewColumn.isRemoteUri ? "" : previewColumn.previewFilePath
        kind: previewColumn.previewKind
        onPdfPageCountChanged: {
            if (previewColumn.pdfPageIndex >= (pdfPreview.pageCount || 0))
                previewColumn.pdfPageIndex = 0
        }
    }

    // Which loader the worker thread should run; "" is metadata only.
    readonly property string previewKind: {
        if (isRemoteUri) return ""
        if (previewIsDir) return "directory"
        if (isArchive) return "archive"
        if (isPdf) return "pdf"
        if (isText) return "text"
        return ""
    }
    property string metadataHint: ""
    property int pdfPageIndex: 0
    property real pdfWheelAccumulator: 0

    readonly property string _mime: fileProps.mimeType || ""
    readonly property bool isRemoteUri: previewFilePath !== "" && fileOps.isRemotePath(previewFilePath)
    readonly property bool isTrashUri: previewFilePath.startsWith("trash:///")
    readonly property bool isArchive: !previewIsDir && fileOps.isArchive(previewFilePath)
    readonly property bool isImage: !isRemoteUri && !previewIsDir && _mime.startsWith("image/")
    readonly property bool isSvg: isImage && _mime === "image/svg+xml"
    readonly property bool isVideo: !isRemoteUri && !previewIsDir && _mime.startsWith("video/")
    readonly property bool isAudio: !isRemoteUri && !previewIsDir && (_mime.startsWith("audio/") || false)
    readonly property bool isPdf: !isRemoteUri && !previewIsDir && _mime === "application/pdf"
    readonly property bool pdfPreviewAvailable: previewService.pdfPreviewAvailable
    readonly property bool videoPreviewAvailable: runtimeFeatures.ffmpegAvailable
    readonly property bool textHighlightAvailable: runtimeFeatures.batAvailable
    readonly property bool hasVisualPreview: isImage || (isVideo && videoPreviewAvailable)
    readonly property string visualSource: {
        if (!hasVisualPreview || previewFilePath === "") return ""
        if (isVideo || isTrashUri || isSvg) return "image://thumbnail/" + encodeURIComponent(previewFilePath)
        return "file://" + previewFilePath
    }
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
    readonly property bool isFont: {
        if (isRemoteUri || previewIsDir)
            return false
        if (_mime.startsWith("font/") || _mime === "application/x-font-ttf"
            || _mime === "application/x-font-otf" || _mime === "application/vnd.ms-fontobject")
            return true
        var ext = previewFileName.lastIndexOf(".") >= 0
            ? previewFileName.substring(previewFileName.lastIndexOf(".") + 1).toLowerCase() : ""
        return ["ttf", "otf", "woff", "woff2"].indexOf(ext) >= 0
    }
    readonly property bool isText: {
        if (isRemoteUri || previewIsDir || isPdf || isImage || isVideo || isAudio || isArchive || isFont) return false
        if (_mime.startsWith("text/")) return true
        var textMimes = [
            "application/json", "application/xml", "application/x-yaml",
            "application/toml", "application/x-shellscript",
            "application/javascript", "application/typescript",
            "application/x-tex", "application/x-makefile",
            "application/x-desktop", "application/x-ruby",
            "application/x-perl", "application/x-python"
        ]
        if (textMimes.indexOf(_mime) >= 0) return true
        var ext = previewFileName.lastIndexOf(".") >= 0
            ? previewFileName.substring(previewFileName.lastIndexOf(".") + 1).toLowerCase() : ""
        if (ext === "") return previewFilePath !== ""
        var textExt = ["txt", "md", "json", "yaml", "yml", "toml", "ini", "cfg", "conf",
                       "sh", "bash", "zsh", "fish", "py", "js", "ts", "tsx", "jsx",
                       "css", "html", "htm", "xml", "c", "cpp", "h", "hpp", "rs",
                       "go", "java", "tex", "rb", "lua", "vim", "log", "diff",
                       "patch", "cmake", "qml", "mk", "desktop"]
        return textExt.indexOf(ext) >= 0
    }

    // The service decides what it rendered: md2html failing falls
    // back to bat, and highlighted source must keep the monospace
    // no-wrap treatment rather than be laid out as a document on the
    // strength of an extension alone.
    readonly property bool isMarkdown: previewColumn.textPreview.markdown === true
    readonly property bool isTable: previewColumn.textPreview.table !== undefined
    // A document's own folder, for the relative image paths a README
    // is written against; without it Qt resolves them against the qrc:
    // path this view was loaded from and draws a broken-image glyph
    // for every one. Segments are encoded so spaces and '#' survive.
    // Qt draws a rich-text image at its native size, so a README's
    // screenshots run far outside the column and push the text off
    // with them. A pixel max-width shrinks anything wider than the
    // column and leaves whatever already fits alone.
    function fitMarkdownImages(html, limit) {
        return html.replace(/<img\b/g,
                            '<img style="max-width:' + Math.max(64, Math.floor(limit)) + 'px"')
    }

    readonly property url markdownBaseUrl: {
        if (!previewColumn.isMarkdown || previewColumn.previewFilePath === "")
            return ""
        var path = previewColumn.previewFilePath
        var dir = path.substring(0, path.lastIndexOf("/"))
        return "file://" + dir.split("/").map(encodeURIComponent).join("/") + "/"
    }

    readonly property string previewFileName: {
        if (fileProps.name)
            return fileProps.name
        if (previewFilePath === "") return ""
        var idx = previewFilePath.lastIndexOf("/")
        return idx >= 0 ? previewFilePath.substring(idx + 1) : previewFilePath
    }

    readonly property var metadataEntries: {
        var result = []
        var md = previewColumn.fileMetadata || {}
        var keys = Object.keys(md)
        for (var i = 0; i < keys.length; ++i) {
            var value = md[keys[i]]
            if (value !== undefined && value !== null && String(value) !== "")
                result.push({ label: keys[i], value: String(value) })
        }
        return result
    }

    readonly property string detailKind: {
        if (previewIsDir) return "Folder"
        if (isArchive) return "Archive"
        if (isAudio) return "Audio"
        if (isVideo) return "Video"
        if (fileProps.mimeDescription) return fileProps.mimeDescription
        var ext = previewFileName.lastIndexOf(".") >= 0
            ? previewFileName.substring(previewFileName.lastIndexOf(".") + 1).toUpperCase() : ""
        return ext !== "" ? ext + " file" : "File"
    }

    function changePdfPage(delta) {
        if (!isPdf || pdfPreview.pageCount <= 0)
            return
        var next = Math.max(0, Math.min(pdfPreview.pageCount - 1, pdfPageIndex + delta))
        if (next === pdfPageIndex)
            return
        pdfHoldSource = pdfImageSource
        pdfPageIndex = next
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

    function refreshPreview() {
        if (previewFilePath === "") {
            fileProps = ({})
            metadataHint = ""
            fontPreview = ({ family: "", styleName: "", weight: 400, italic: false, valid: false, error: "" })
            previewLoader.stop()
            return
        }

        // Always go through the canonical fsModel for property
        // lookup. Other models that can feed miller view
        // (RecentFilesModel, SearchProxyModel) don't expose
        // fileProperties(), and fsModel.fileProperties() already
        // handles local / trash:// / remote URIs internally.
        // This is an in-memory lookup, so the info bar updates
        // immediately while the body is still being read.
        if (fsModel && fsModel.fileProperties)
            fileProps = fsModel.fileProperties(previewFilePath)
        else
            fileProps = ({})

        metadataHint = isRemoteUri
            ? "" : metadataExtractor.missingDepsHint(fileProps.mimeType || "")

        fontPreview = (isFont && !isRemoteUri)
            ? previewService.loadFontPreview(previewFilePath)
            : ({ family: "", styleName: "", weight: 400, italic: false, valid: false, error: "" })

        previewLoader.reload()
    }

    onPreviewFilePathChanged: {
        directoryModel.setRootPath(previewIsDir ? previewFilePath : "")
        pdfHoldSource = ""
        pdfPageIndex = 0
        pdfWheelAccumulator = 0
        refreshPreview()
    }
    onPreviewIsDirChanged: directoryModel.setRootPath(previewIsDir ? previewFilePath : "")

    // ── Preview content area (top) + info bar (bottom) ───────────
    Column {
        anchors.fill: parent

        // Preview area
        Item {
            id: previewArea
            width: parent.width
            height: parent.height - infoBar.height

            // Directory listing
            ListView {
                id: previewDirList
                anchors.fill: parent
                visible: previewColumn.previewIsDir
                model: previewColumn.directoryModel
                clip: true
                reuseItems: true
                cacheBuffer: 256
                interactive: true
                boundsBehavior: Flickable.StopAtBounds

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    width: previewDirList.width
                    height: 24

                    required property int index
                    required property string fileName
                    required property bool isDir
                    required property string fileIconName

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 4
                        spacing: 6

                        Image {
                            width: 14; height: 14
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://icon/" + fileIconName + "?theme=" + config.iconTheme + "&r=" + config.iconRevision
                            opacity: fileName.startsWith(".") ? Theme.hiddenIconOpacity : 1
                            sourceSize: Qt.size(14 * Screen.devicePixelRatio,
                                                14 * Screen.devicePixelRatio)
                            asynchronous: true
                        }

                        Text {
                            width: parent.width - 14 - parent.spacing - parent.anchors.leftMargin - parent.anchors.rightMargin
                            anchors.verticalCenter: parent.verticalCenter
                            textFormat: Text.PlainText
                            text: fileName
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // Archive contents preview
            Item {
                anchors.fill: parent
                visible: previewColumn.isArchive

                Text {
                    id: archivePreviewTitle
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 8
                    text: "Archive contents"
                    color: Theme.text
                    font.pointSize: Theme.fontSmall
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    anchors.top: archivePreviewTitle.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 8
                    visible: previewColumn.directoryPreview.error !== ""
                    textFormat: Text.PlainText
                    text: previewColumn.directoryPreview.error
                    color: Theme.error
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }

                ListView {
                    id: archivePreviewList
                    anchors.top: archivePreviewTitle.bottom
                    anchors.topMargin: 8
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    visible: previewColumn.directoryPreview.error === ""
                    model: previewColumn.directoryPreview.entries || []
                    clip: true
                    spacing: 4

                    delegate: Text {
                        width: archivePreviewList.width - 12
                        text: modelData
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                        elide: Text.ElideMiddle
                    }

                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                }
            }

            // Image/Video preview
            Image {
                id: visualPreview
                anchors.fill: parent
                anchors.margins: 8
                visible: previewColumn.hasVisualPreview && !previewColumn.previewIsDir && !previewColumn.isPdf
                source: previewColumn.visualSource
                sourceSize: Qt.size(width, height)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
            }

            // Outgoing page, drawn underneath while the next one
            // loads. Cleared as soon as pdfPreviewImage is Ready.
            Image {
                id: pdfHoldImage
                anchors.fill: parent
                anchors.margins: 8
                visible: previewColumn.pdfHoldSource !== ""
                    && pdfPreviewImage.status !== Image.Ready
                source: previewColumn.pdfHoldSource
                sourceSize: pdfPreviewImage.sourceSize
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
            }

            // PDF preview
            Image {
                id: pdfPreviewImage
                anchors.fill: parent
                anchors.margins: 8
                visible: previewColumn.isPdf
                    && previewColumn.pdfPreviewAvailable
                    && previewColumn.pdfPreview.localPath !== ""
                    && previewColumn.pdfPreview.error === ""
                source: previewColumn.pdfImageSource
                sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                onStatusChanged: {
                    if (status === Image.Ready || status === Image.Error)
                        previewColumn.pdfHoldSource = ""
                }
            }

            // Warm the neighbouring pages into Qt's pixmap cache so
            // flipping is a cache hit instead of another pdftoppm run.
            // sourceSize must match pdfPreviewImage exactly -- it is
            // part of the cache key, and a mismatch silently renders
            // every page twice instead of caching anything.
            Repeater {
                model: pdfPreviewImage.visible ? [1, -1] : []
                Image {
                    visible: false
                    asynchronous: true
                    sourceSize: pdfPreviewImage.sourceSize
                    source: {
                        var page = previewColumn.pdfPageIndex + modelData
                        if (page < 0 || page >= (previewColumn.pdfPreview.pageCount || 0))
                            return ""
                        return "image://pdfpreview/"
                            + encodeURIComponent(previewColumn.pdfPreview.localPath)
                            + "?page=" + page
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                visible: pdfPreviewImage.visible
                acceptedButtons: Qt.NoButton
                onWheel: (wheel) => previewColumn.handlePdfWheel(wheel)
            }

            Row {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 10
                anchors.rightMargin: 10
                spacing: 6
                visible: pdfPreviewImage.visible && previewColumn.pdfPreview.pageCount > 1

                Rectangle {
                    width: 26
                    height: 26
                    radius: 13
                    enabled: previewColumn.pdfPageIndex > 0
                    opacity: enabled ? 1 : 0.45
                    color: pdfPrevMouse.containsMouse
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                        : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                    IconChevronUp {
                        anchors.centerIn: parent
                        size: 14
                        color: Theme.text
                    }

                    MouseArea {
                        id: pdfPrevMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: parent.enabled
                        onClicked: previewColumn.changePdfPage(-1)
                    }
                }

                Rectangle {
                    width: 26
                    height: 26
                    radius: 13
                    enabled: previewColumn.pdfPageIndex < previewColumn.pdfPreview.pageCount - 1
                    opacity: enabled ? 1 : 0.45
                    color: pdfNextMouse.containsMouse
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                        : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                    IconChevronDown {
                        anchors.centerIn: parent
                        size: 14
                        color: Theme.text
                    }

                    MouseArea {
                        id: pdfNextMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: parent.enabled
                        onClicked: previewColumn.changePdfPage(1)
                    }
                }
            }

            Text {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 10
                visible: pdfPreviewImage.visible && previewColumn.pdfPageLabel !== ""
                text: previewColumn.pdfPageLabel
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
            }

            Rectangle {
                anchors.centerIn: parent
                visible: (previewColumn.hasVisualPreview && visualPreview.status === Image.Loading)
                    || (pdfPreviewImage.visible && pdfPreviewImage.status === Image.Loading)
                color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.72)
                radius: Theme.radiusMedium
                width: 170
                height: 40

                Text {
                    anchors.centerIn: parent
                    text: previewColumn.isPdf ? "Rendering PDF..." : "Loading preview..."
                    color: Theme.text
                    font.pointSize: Theme.fontSmall
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: 8
                visible: (previewColumn.hasVisualPreview && visualPreview.status === Image.Error)
                    || (pdfPreviewImage.visible && pdfPreviewImage.status === Image.Error)

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 64; height: 64
                    source: "image://icon/" + (previewColumn.fileProps.iconName || (previewColumn.isPdf ? "application-pdf" : "image-x-generic")) + "?theme=" + config.iconTheme + "&r=" + config.iconRevision
                    sourceSize: Qt.size(64, 64)
                    asynchronous: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: previewColumn.isPdf ? "PDF preview could not be loaded" : "Preview could not be loaded"
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                }
            }

            // Text preview
            Flickable {
                id: textPreviewFlick
                anchors.fill: parent
                anchors.margins: 6
                visible: previewColumn.isText && !previewColumn.isTable && !previewColumn.hasVisualPreview
                    && !previewColumn.previewIsDir && !previewColumn.isPdf && !previewColumn.isArchive
                clip: true
                interactive: true
                boundsMovement: Flickable.StopAtBounds
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: previewColumn.isMarkdown ? markdownWidth : Math.max(width, textArea.implicitWidth)
                contentHeight: Math.max(height, textArea.implicitHeight)

                // The width Markdown is laid out at, caught up once the
                // panel stops resizing. Opening a split animates this
                // column every frame, and each new width rebuilt the
                // HTML (image limits) and re-laid out the whole
                // document: long READMEs made the animation crawl.
                property real markdownWidth: width
                onWidthChanged: markdownSettle.restart()
                Timer {
                    id: markdownSettle
                    interval: 150
                    onTriggered: textPreviewFlick.markdownWidth = textPreviewFlick.width
                }

                TextEdit {
                    id: textArea
                    readOnly: true
                    selectByMouse: true
                    width: previewColumn.isMarkdown ? textPreviewFlick.markdownWidth : Math.max(implicitWidth, textPreviewFlick.width)
                    baseUrl: previewColumn.markdownBaseUrl
                    height: Math.max(implicitHeight, textPreviewFlick.height)
                    textFormat: previewColumn.textPreview.usesBat && previewColumn.textPreview.html !== ""
                        ? TextEdit.RichText
                        : TextEdit.PlainText
                    text: previewColumn.textPreview.error !== ""
                        ? previewColumn.textPreview.error
                        : (previewColumn.textPreview.isBinary
                            ? "This file looks binary and cannot be previewed as text."
                            : (previewColumn.textPreview.usesBat && previewColumn.textPreview.html !== ""
                                ? (previewColumn.isMarkdown
                                    ? previewColumn.fitMarkdownImages(
                                          previewColumn.textPreview.html,
                                          textPreviewFlick.markdownWidth - 24)
                                    : previewColumn.textPreview.html)
                                : previewColumn.textPreview.content))
                    color: Theme.text
                    wrapMode: previewColumn.isMarkdown ? TextEdit.WrapAtWordBoundaryOrAnywhere : TextEdit.NoWrap
                    font.family: previewColumn.isMarkdown ? Qt.application.font.family : "monospace"
                    font.pointSize: Theme.fontSmall - 1

                    onCursorRectangleChanged: {
                        var r = cursorRectangle
                        var pad = 4
                        if (r.x - pad < textPreviewFlick.contentX)
                            textPreviewFlick.contentX = Math.max(0, r.x - pad)
                        else if (r.x + r.width + pad > textPreviewFlick.contentX + textPreviewFlick.width)
                            textPreviewFlick.contentX = Math.min(
                                textPreviewFlick.contentWidth - textPreviewFlick.width,
                                r.x + r.width + pad - textPreviewFlick.width)
                        if (r.y - pad < textPreviewFlick.contentY)
                            textPreviewFlick.contentY = Math.max(0, r.y - pad)
                        else if (r.y + r.height + pad > textPreviewFlick.contentY + textPreviewFlick.height)
                            textPreviewFlick.contentY = Math.min(
                                textPreviewFlick.contentHeight - textPreviewFlick.height,
                                r.y + r.height + pad - textPreviewFlick.height)
                    }
                }

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
            }

            CsvTable {
                anchors.fill: parent
                anchors.margins: 6
                visible: previewColumn.isText && previewColumn.isTable && !previewColumn.hasVisualPreview
                    && !previewColumn.previewIsDir && !previewColumn.isPdf && !previewColumn.isArchive
                table: visible ? previewColumn.textPreview.table : ({})
                fontSize: Theme.fontSmall - 1
            }

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 8
                visible: textPreviewFlick.visible
                    && previewColumn.textPreview.error === ""
                    && !previewColumn.textPreview.isBinary
                    && !previewColumn.textPreview.usesBat
                    && !previewColumn.textHighlightAvailable
                text: runtimeFeatures.installHint("textHighlight")
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                horizontalAlignment: Text.AlignHCenter
            }

            Column {
                anchors.centerIn: parent
                spacing: 10
                visible: previewColumn.isPdf
                    && (!previewColumn.pdfPreviewAvailable || previewColumn.pdfPreview.error !== "")

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 72; height: 72
                    source: "image://icon/" + (previewColumn.fileProps.iconName || "application-pdf") + "?theme=" + config.iconTheme + "&r=" + config.iconRevision
                    sourceSize: Qt.size(72, 72)
                    asynchronous: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 220
                    text: previewColumn.pdfPreviewAvailable
                        ? "PDF preview is unavailable for this file"
                        : "PDF preview support is unavailable"
                    color: Theme.text
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 220
                    textFormat: Text.PlainText
                    text: previewColumn.pdfPreview.error !== ""
                        ? previewColumn.pdfPreview.error
                        : runtimeFeatures.installHint("pdfPreview")
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Font preview
            Flickable {
                id: millerFontFlick
                anchors.fill: parent
                anchors.margins: 10
                visible: previewColumn.isFont
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width
                contentHeight: millerFontColumn.implicitHeight

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: millerFontColumn
                    width: millerFontFlick.width
                    spacing: 10

                    Text {
                        width: parent.width
                        visible: !previewColumn.fontPreview.valid
                        textFormat: Text.PlainText
                        text: previewColumn.fontPreview.error || "Unable to load this font"
                        color: Theme.error
                        font.pointSize: Theme.fontSmall
                    }

                    Text {
                        width: parent.width
                        visible: previewColumn.fontPreview.valid
                        text: previewColumn.fontPreview.styleName && previewColumn.fontPreview.styleName !== ""
                            ? (previewColumn.fontPreview.family + " — " + previewColumn.fontPreview.styleName)
                            : (previewColumn.fontPreview.family || "")
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Repeater {
                        model: previewColumn.fontPreview.valid ? [12, 16, 22, 30] : []
                        delegate: Text {
                            width: millerFontColumn.width
                            text: "The quick brown fox jumps over the lazy dog"
                            color: Theme.text
                            font.family: previewColumn.fontPreview.family || ""
                            font.styleName: previewColumn.fontPreview.styleName || ""
                            font.weight: previewColumn.fontPreview.weight || Font.Normal
                            font.italic: previewColumn.fontPreview.italic || false
                            font.pointSize: modelData
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                    }

                    Text {
                        width: parent.width
                        visible: previewColumn.fontPreview.valid
                        text: "ABCDEFGHIJKLMNOPQRSTUVWXYZ\nabcdefghijklmnopqrstuvwxyz\n0123456789"
                        color: Theme.text
                        font.family: previewColumn.fontPreview.family || ""
                        font.styleName: previewColumn.fontPreview.styleName || ""
                        font.weight: previewColumn.fontPreview.weight || Font.Normal
                        font.italic: previewColumn.fontPreview.italic || false
                        font.pointSize: 16
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    Text {
                        width: parent.width
                        visible: previewColumn.fontPreview.valid
                        text: "Glyphs & ligatures"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                        font.weight: Font.DemiBold
                        topPadding: 4
                    }

                    Repeater {
                        model: previewColumn.fontPreview.valid
                            ? [
                                "~!@#$%^&* {} [] () I1l O0o",
                                "!== \\\\ <= #{ -> ~@ |> 0x12",
                                "|=>==<==>=|======|===|===>",
                                "<---|--|--------|-<->--<-|",
                                "[INFO] todo)) fixme))"
                              ]
                            : []
                        delegate: Text {
                            width: millerFontColumn.width
                            text: modelData
                            color: Theme.text
                            font.family: previewColumn.fontPreview.family || ""
                            font.styleName: previewColumn.fontPreview.styleName || ""
                            font.weight: previewColumn.fontPreview.weight || Font.Normal
                            font.italic: previewColumn.fontPreview.italic || false
                            font.pointSize: 14
                            wrapMode: Text.NoWrap
                        }
                    }
                }
            }

            // Fallback: icon for non-previewable files
            Column {
                anchors.centerIn: parent
                spacing: 10
                visible: !previewColumn.previewIsDir && !previewColumn.isArchive
                    && !previewColumn.hasVisualPreview && !previewColumn.isText
                    && !previewColumn.isPdf && !previewColumn.isFont
                    && previewColumn.previewFilePath !== ""

                Image {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 64; height: 64
                    source: previewColumn.fileProps.iconName
                        ? ("image://icon/" + previewColumn.fileProps.iconName + "?theme=" + config.iconTheme + "&r=" + config.iconRevision)
                        : ("image://icon/text-x-generic?theme=" + config.iconTheme + "&r=" + config.iconRevision)
                    sourceSize: Qt.size(64, 64)
                    asynchronous: true
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 220
                    text: previewColumn.isAudio
                        ? "Audio preview is not available yet"
                        : (previewColumn.isVideo && !previewColumn.videoPreviewAvailable
                            ? "Video preview support is unavailable"
                            : "Preview not available")
                    color: Theme.text
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 220
                    text: previewColumn.isVideo && !previewColumn.videoPreviewAvailable
                        ? runtimeFeatures.installHint("videoPreview")
                        : "Open Quick Preview for a larger preview surface."
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                visible: previewColumn.previewFilePath === ""
                text: "No selection"
                color: Qt.rgba(Theme.subtext.r, Theme.subtext.g, Theme.subtext.b, 0.5)
                font.pointSize: Theme.fontSmall
            }
        }

        // Info bar at bottom
        Rectangle {
            id: infoBar
            width: parent.width
            height: previewColumn.previewFilePath !== ""
                ? Math.min(parent.height * 0.34, Math.max(54, infoBarContent.implicitHeight + 14))
                : 0
            visible: previewColumn.previewFilePath !== ""
            color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.5)
            border.width: 0

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: 1
                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
            }

            Flickable {
                id: infoBarFlick
                anchors.fill: parent
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                anchors.topMargin: 7
                anchors.bottomMargin: 7
                clip: true
                interactive: contentHeight > height
                boundsMovement: Flickable.StopAtBounds
                boundsBehavior: Flickable.StopAtBounds
                contentWidth: width
                contentHeight: infoBarContent.implicitHeight

                Column {
                    id: infoBarContent
                    width: infoBarFlick.width
                    spacing: 4

                    Text {
                        width: parent.width
                        text: previewColumn.previewFileName
                        color: Theme.text
                        font.pointSize: Theme.fontSmall
                        font.bold: true
                        elide: Text.ElideMiddle
                    }

                    Text {
                        width: parent.width
                        text: {
                            var parts = []
                            parts.push(previewColumn.detailKind)
                            if (previewColumn.fileProps.sizeText)
                                parts.push(previewColumn.fileProps.sizeText)
                            if (previewColumn.previewIsDir && previewColumn.fileProps.contentText)
                                parts.push(previewColumn.fileProps.contentText)
                            if (previewColumn.pdfPageLabel !== "")
                                parts.push(previewColumn.pdfPageLabel)
                            if (previewColumn.fileProps.modified)
                                parts.push(previewColumn.fileProps.modified)
                            return parts.join(" \u00b7 ")
                        }
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall - 1
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    Text {
                        width: parent.width
                        visible: !!(previewColumn.fileProps.originalPath || previewColumn.fileProps.parentDir)
                        text: (previewColumn.fileProps.originalPath ? "Original Location: " : "Location: ")
                            + (previewColumn.fileProps.originalPath || previewColumn.fileProps.parentDir || "")
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall - 1
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    Text {
                        width: parent.width
                        visible: !previewColumn.previewIsDir && !!previewColumn.fileProps.contentText
                        text: "Contents: " + previewColumn.fileProps.contentText
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall - 1
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    Text {
                        width: parent.width
                        visible: !!previewColumn.fileProps.deleted
                        text: "Deleted: " + previewColumn.fileProps.deleted
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall - 1
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    Repeater {
                        model: previewColumn.metadataEntries

                        delegate: Text {
                            width: infoBarContent.width
                            text: modelData.label + ": " + modelData.value
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall - 1
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                    }

                    Text {
                        width: parent.width
                        visible: previewColumn.metadataHint !== ""
                        text: previewColumn.metadataHint
                        color: Theme.muted
                        font.pointSize: Theme.fontSmall - 1
                        font.italic: true
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    Text {
                        width: parent.width
                        visible: previewColumn.isText && previewColumn.textPreview.truncated
                        text: "Showing a shortened text preview for quick browsing."
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall - 1
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }

                    Text {
                        width: parent.width
                        visible: (previewColumn.previewIsDir || previewColumn.isArchive) && previewColumn.directoryPreview.truncated
                        text: "Only the first items are shown here."
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall - 1
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                }

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            }
        }
    }
}
