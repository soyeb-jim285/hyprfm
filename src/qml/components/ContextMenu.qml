import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

// Custom context menu — Hyprland compositor handles blur via windowrule
Item {
    id: root
    anchors.fill: parent
    visible: false
    z: 9999

    property string targetPath: ""
    property bool targetIsDir: false
    property bool isEmptySpace: false
    property var selectedPaths: []

    signal openRequested(string path)
    signal cutRequested(var paths)
    signal copyRequested(var paths)
    signal pasteRequested(string destPath)
    signal copyPathRequested(string path)
    signal renameRequested(string path)
    signal trashRequested(var paths)
    signal deleteRequested(var paths)
    signal openInTerminalRequested(string path)
    signal newFolderRequested(string parentPath)
    signal newFileRequested(string parentPath)
    signal selectAllRequested()
    signal propertiesRequested(string path)

    property var effectivePaths: (selectedPaths.length > 0) ? selectedPaths : (targetPath !== "" ? [targetPath] : [])
    property string effectiveDir: {
        if (isEmptySpace) return targetPath
        if (targetIsDir) return targetPath
        var p = targetPath
        var idx = p.lastIndexOf("/")
        return idx > 0 ? p.substring(0, idx) : "/"
    }

    property Item blurSource: null

    function popup(x, y) {
        var menuW = menuColumn.width + 16
        var menuH = menuColumn.height + 16
        menuContainer.x = Math.min(x, root.width - menuW - 8)
        menuContainer.y = Math.min(y, root.height - menuH - 8)
        root.visible = true
        openAnim.start()
    }

    function close() {
        closeAnim.start()
    }

    // ── Open animation ────────────────────────────────────────────────────
    ParallelAnimation {
        id: openAnim
        NumberAnimation {
            target: menuContainer; property: "opacity"
            from: 0; to: 1; duration: 180
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: menuContainer; property: "scale"
            from: 0.88; to: 1; duration: 250
            easing.type: Easing.OutBack
            easing.overshoot: 0.8
        }
        NumberAnimation {
            target: menuContainer; property: "yOffset"
            from: -8; to: 0; duration: 220
            easing.type: Easing.OutCubic
        }
    }

    // ── Close animation ───────────────────────────────────────────────────
    SequentialAnimation {
        id: closeAnim
        ParallelAnimation {
            NumberAnimation {
                target: menuContainer; property: "opacity"
                to: 0; duration: 120
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: menuContainer; property: "scale"
                to: 0.92; duration: 120
                easing.type: Easing.InCubic
            }
            NumberAnimation {
                target: menuContainer; property: "yOffset"
                to: -4; duration: 120
                easing.type: Easing.InCubic
            }
        }
        ScriptAction { script: root.visible = false }
    }

    // Click outside to close
    MouseArea {
        anchors.fill: parent
        onClicked: root.close()
        onWheel: (wheel) => { wheel.accepted = true }
    }

    // ── Menu container ────────────────────────────────────────────────────
    Item {
        id: menuContainer
        x: 0
        y: 0
        width: menuColumn.width + 16
        height: menuColumn.height + 16

        opacity: 0
        scale: 0.88
        transformOrigin: Item.TopLeft

        // Vertical slide offset driven by animation
        property real yOffset: 0
        transform: Translate { y: menuContainer.yOffset }

        // Background
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLarge
            color: Theme.crust
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
            border.width: 1
        }

        // ── Menu items with staggered entrance ──────────────────────────
        Column {
            id: menuColumn
            anchors.centerIn: parent
            width: 260
            spacing: 2

            Repeater {
                model: root.visible ? root.buildModel() : []
                delegate: Loader {
                    id: delegateLoader
                    width: menuColumn.width
                    sourceComponent: modelData.separator ? separatorComponent : itemComponent
                    property var itemData: modelData
                    property int itemIndex: index

                }
            }
        }
    }

    function buildModel() {
        var items = []
        if (!isEmptySpace && targetPath !== "") {
            items.push({ text: "Open", shortcut: "Return", action: "open" })
            if (targetIsDir)
                items.push({ text: "Open in Terminal", shortcut: "", action: "terminal" })
            items.push({ separator: true })
            items.push({ text: "Cut", shortcut: "Ctrl+X", action: "cut" })
            items.push({ text: "Copy", shortcut: "Ctrl+C", action: "copy" })
            items.push({ text: "Copy Path", shortcut: "", action: "copypath" })
            items.push({ separator: true })
            items.push({ text: "Rename...", shortcut: "F2", action: "rename" })
            items.push({ text: "Move to Trash", shortcut: "Delete", action: "trash" })
            items.push({ separator: true })
            items.push({ text: "Properties", shortcut: "", action: "properties" })
        } else {
            items.push({ text: "New Folder...", shortcut: "Shift+Ctrl+N", action: "newfolder" })
            items.push({ text: "New File...", shortcut: "", action: "newfile" })
            items.push({ separator: true })
            if (clipboard.hasContent)
                items.push({ text: "Paste", shortcut: "Ctrl+V", action: "paste" })
            items.push({ text: "Select All", shortcut: "Ctrl+A", action: "selectall" })
            items.push({ separator: true })
            items.push({ text: "Open in Terminal", shortcut: "", action: "terminal" })
            items.push({ text: "Properties", shortcut: "", action: "properties" })
        }
        return items
    }

    function executeAction(action) {
        root.close()
        switch (action) {
        case "open": openRequested(targetPath); break
        case "cut": cutRequested(effectivePaths); break
        case "copy": copyRequested(effectivePaths); break
        case "copypath": copyPathRequested(targetPath); break
        case "rename": renameRequested(targetPath); break
        case "trash": trashRequested(effectivePaths); break
        case "delete": deleteRequested(effectivePaths); break
        case "paste": pasteRequested(effectiveDir); break
        case "selectall": selectAllRequested(); break
        case "terminal": openInTerminalRequested(effectiveDir); break
        case "newfolder": newFolderRequested(effectiveDir); break
        case "newfile": newFileRequested(effectiveDir); break
        case "properties": propertiesRequested(targetPath); break
        }
    }

    Component {
        id: itemComponent
        Rectangle {
            height: 32
            width: parent ? parent.width : 260
            radius: Theme.radiusMedium
            color: itemMa.containsMouse
                ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                : "transparent"
            Behavior on color {
                ColorAnimation { duration: 100; easing.type: Easing.OutCubic }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 16
                Text {
                    text: itemData ? itemData.text : ""
                    font.pixelSize: Theme.fontNormal
                    color: Theme.text
                    Layout.fillWidth: true
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    text: itemData ? (itemData.shortcut || "") : ""
                    font.pixelSize: Theme.fontSmall
                    color: Theme.muted
                    visible: text !== ""
                    verticalAlignment: Text.AlignVCenter
                }
            }
            MouseArea {
                id: itemMa
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (itemData && itemData.action)
                        root.executeAction(itemData.action)
                }
            }
        }
    }

    Component {
        id: separatorComponent
        Item {
            height: 9
            width: parent ? parent.width : 260
            Rectangle {
                anchors.centerIn: parent
                width: parent.width - 16
                height: 1
                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
            }
        }
    }
}
