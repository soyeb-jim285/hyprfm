import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

Menu {
    id: root

    // Fade + scale animation on open/close
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.92; to: 1.0; duration: 150; easing.type: Easing.OutCubic }
        }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100; easing.type: Easing.InCubic }
    }

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

    // ── Styled components ─────────────────────────────────────────────────
    component StyledMenuItem: MenuItem {
        id: styledItem
        property color textColor: Theme.text
        property string shortcutText: ""
        leftPadding: 6
        rightPadding: 6
        topPadding: 2
        bottomPadding: 2
        contentItem: RowLayout {
            spacing: 16
            Text {
                text: styledItem.text
                font.pixelSize: Theme.fontNormal
                color: styledItem.enabled ? styledItem.textColor : Theme.muted
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                leftPadding: 8
            }
            Text {
                text: styledItem.shortcutText
                font.pixelSize: Theme.fontSmall
                color: Theme.muted
                verticalAlignment: Text.AlignVCenter
                visible: styledItem.shortcutText !== ""
                rightPadding: 8
            }
        }
        background: Rectangle {
            implicitHeight: 30
            implicitWidth: 240
            color: styledItem.highlighted
                ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                : "transparent"
            radius: Theme.radiusMedium

            Behavior on color {
                ColorAnimation { duration: 150; easing.type: Easing.OutCubic }
            }
        }
    }

    component StyledSeparator: MenuSeparator {
        topPadding: 4
        bottomPadding: 4
        contentItem: Rectangle {
            implicitHeight: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
        }
        background: Rectangle { color: "transparent" }
    }

    background: Rectangle {
        implicitWidth: 260
        color: Qt.rgba(Theme.crust.r, Theme.crust.g, Theme.crust.b, 0.75)
        radius: Theme.radiusLarge
        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
        border.width: 1
    }

    // ══════════════════════════════════════════════════════════════════════
    // RIGHT-CLICK ON FILE/FOLDER
    // ══════════════════════════════════════════════════════════════════════

    StyledMenuItem {
        text: "Open"
        shortcutText: "Return"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.openRequested(root.targetPath)
    }

    StyledMenuItem {
        text: "Open in Terminal"
        visible: !isEmptySpace && targetIsDir
        height: visible ? implicitHeight : 0
        onTriggered: root.openInTerminalRequested(root.targetPath)
    }

    StyledSeparator {
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
    }

    StyledMenuItem {
        text: "Cut"
        shortcutText: "Ctrl+X"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.cutRequested(root.effectivePaths)
    }

    StyledMenuItem {
        text: "Copy"
        shortcutText: "Ctrl+C"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.copyRequested(root.effectivePaths)
    }

    StyledMenuItem {
        text: "Copy Path"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.copyPathRequested(root.targetPath)
    }

    StyledSeparator {
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
    }

    StyledMenuItem {
        text: "Rename..."
        shortcutText: "F2"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.renameRequested(root.targetPath)
    }

    StyledMenuItem {
        text: "Move to Trash"
        shortcutText: "Delete"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.trashRequested(root.effectivePaths)
    }

    StyledSeparator {
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
    }

    StyledMenuItem {
        text: "Properties"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.propertiesRequested(root.targetPath)
    }

    // ══════════════════════════════════════════════════════════════════════
    // RIGHT-CLICK ON EMPTY SPACE
    // ══════════════════════════════════════════════════════════════════════

    StyledMenuItem {
        text: "New Folder..."
        shortcutText: "Shift+Ctrl+N"
        visible: isEmptySpace
        height: visible ? implicitHeight : 0
        onTriggered: root.newFolderRequested(root.effectiveDir)
    }

    StyledMenuItem {
        text: "New File..."
        visible: isEmptySpace
        height: visible ? implicitHeight : 0
        onTriggered: root.newFileRequested(root.effectiveDir)
    }

    StyledSeparator {
        visible: isEmptySpace
        height: visible ? implicitHeight : 0
    }

    StyledMenuItem {
        text: "Paste"
        shortcutText: "Ctrl+V"
        visible: isEmptySpace
        enabled: clipboard.hasContent
        height: visible ? implicitHeight : 0
        onTriggered: root.pasteRequested(root.effectiveDir)
    }

    StyledMenuItem {
        text: "Select All"
        shortcutText: "Ctrl+A"
        visible: isEmptySpace
        height: visible ? implicitHeight : 0
        onTriggered: root.selectAllRequested()
    }

    StyledSeparator {
        visible: isEmptySpace
        height: visible ? implicitHeight : 0
    }

    StyledMenuItem {
        text: "Open in Terminal"
        visible: isEmptySpace
        height: visible ? implicitHeight : 0
        onTriggered: root.openInTerminalRequested(root.effectiveDir)
    }

    StyledMenuItem {
        text: "Properties"
        visible: isEmptySpace
        height: visible ? implicitHeight : 0
        onTriggered: root.propertiesRequested(root.effectiveDir)
    }

    // ── Custom actions from config ────────────────────────────────────────
    Instantiator {
        model: config.customContextActions
        delegate: StyledMenuItem {
            required property var modelData
            text: modelData.name ?? ""
            onTriggered: {
                if (modelData.command) {
                    var cmd = modelData.command.replace("{file}", root.targetPath)
                    fileOps.openFile(cmd)
                }
            }
        }
        onObjectAdded: (index, object) => root.addItem(object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
