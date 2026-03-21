import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

Item {
    id: root

    // Public interface
    property string filePath: ""
    property var directoryFiles: []   // list of file paths in current dir for cycling
    property bool active: false

    signal closed()

    // ── Derived state ───────────────────────────────────────────────────────
    readonly property string fileName: {
        if (filePath === "") return ""
        var idx = filePath.lastIndexOf("/")
        return idx >= 0 ? filePath.substring(idx + 1) : filePath
    }

    readonly property string fileExtension: {
        var name = fileName
        var dot = name.lastIndexOf(".")
        return dot >= 0 ? name.substring(dot + 1).toLowerCase() : ""
    }

    readonly property bool isImage: {
        var img = ["png", "jpg", "jpeg", "gif", "bmp", "webp", "svg", "ico", "tiff", "tif"]
        return img.indexOf(fileExtension) >= 0
    }

    readonly property bool isText: {
        var txt = ["txt", "md", "json", "yaml", "yml", "toml", "ini", "cfg", "conf",
                   "sh", "bash", "zsh", "fish", "py", "js", "ts", "css", "html",
                   "htm", "xml", "c", "cpp", "h", "hpp", "rs", "go", "java",
                   "rb", "lua", "vim", "log", "diff", "patch", "cmake", "qml"]
        return txt.indexOf(fileExtension) >= 0
    }

    // ── Visibility / focus ──────────────────────────────────────────────────
    visible: active
    focus: active

    // ── Arrow-key cycling ───────────────────────────────────────────────────
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

    Keys.onEscapePressed: { active = false; root.closed() }
    Keys.onLeftPressed:  cycleFile(-1)
    Keys.onRightPressed: cycleFile(1)

    // ── Semi-transparent backdrop ───────────────────────────────────────────
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.6)
        opacity: overlayOpacity.running ? 0 : 1

        // Fade-in
        NumberAnimation on opacity {
            id: overlayOpacity
            from: 0; to: 1
            duration: 160
            easing.type: Easing.OutCubic
            running: root.active
        }

        // Click backdrop to close
        MouseArea {
            anchors.fill: parent
            onClicked: { root.active = false; root.closed() }
        }
    }

    // ── Centered card ───────────────────────────────────────────────────────
    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, 900)
        height: Math.min(parent.height * 0.85, 700)
        color: Theme.mantle
        radius: Theme.radiusLarge
        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
        border.width: 1

        // Stop backdrop clicks from propagating through card
        MouseArea {
            anchors.fill: parent
            onClicked: { /* absorb */ }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            // ── Header ──────────────────────────────────────────────────────
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: root.fileName
                    color: Theme.text
                    font.pixelSize: Theme.fontLarge
                    font.bold: true
                    elide: Text.ElideMiddle
                }

                // Arrow navigation hints
                Text {
                    text: "← →"
                    color: Theme.muted
                    font.pixelSize: Theme.fontSmall
                    visible: root.directoryFiles.length > 1
                }

                // Close button
                Rectangle {
                    width: 28; height: 28
                    radius: 14
                    color: closeMa.containsMouse
                        ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.2)
                        : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "✕"
                        color: Theme.error
                        font.pixelSize: Theme.fontNormal
                    }

                    MouseArea {
                        id: closeMa
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: { root.active = false; root.closed() }
                    }
                }
            }

            // Separator
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
            }

            // ── Content area ─────────────────────────────────────────────────
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // Image preview
                Image {
                    anchors.fill: parent
                    visible: root.isImage
                    source: root.isImage && root.filePath !== "" ? "file://" + root.filePath : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true

                    Rectangle {
                        anchors.centerIn: parent
                        visible: parent.status === Image.Loading
                        color: "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: "Loading…"
                            color: Theme.muted
                            font.pixelSize: Theme.fontNormal
                        }
                    }
                }

                // Text preview (placeholder — reading via URL directly)
                Flickable {
                    anchors.fill: parent
                    visible: root.isText && !root.isImage
                    contentWidth: width
                    contentHeight: textContent.height
                    clip: true

                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                    Text {
                        id: textContent
                        width: parent.width
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        color: Theme.text
                        font.family: "monospace"
                        font.pixelSize: Theme.fontSmall
                        // For MVP: show file path info; a C++ helper is needed for
                        // reading file content in production.
                        text: root.isText ? "Text preview for:\n" + root.filePath + "\n\n(File content reading requires a C++ helper in production builds.)" : ""
                    }
                }

                // Unknown type fallback
                Item {
                    anchors.fill: parent
                    visible: !root.isImage && !root.isText

                    Column {
                        anchors.centerIn: parent
                        spacing: 12

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "📄"
                            font.pixelSize: 64
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "Preview not available"
                            color: Theme.subtext
                            font.pixelSize: Theme.fontNormal
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: root.fileName
                            color: Theme.muted
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }

            // ── Footer / file info ─────────────────────────────────────────
            Text {
                Layout.fillWidth: true
                text: root.directoryFiles.length > 1
                    ? (root.currentIndex() + 1) + " / " + root.directoryFiles.length + "  ·  Use ← → to navigate"
                    : ""
                color: Theme.muted
                font.pixelSize: Theme.fontSmall
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
