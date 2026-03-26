import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

Menu {
    id: root

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
        contentItem: RowLayout {
            spacing: 24
            Text {
                text: styledItem.text
                font.pixelSize: Theme.fontNormal
                color: styledItem.enabled ? styledItem.textColor : Theme.muted
                verticalAlignment: Text.AlignVCenter
                Layout.fillWidth: true
                leftPadding: 12
            }
            Text {
                text: styledItem.shortcutText
                font.pixelSize: Theme.fontSmall
                color: Theme.muted
                verticalAlignment: Text.AlignVCenter
                visible: styledItem.shortcutText !== ""
                rightPadding: 12
            }
        }
        background: Rectangle {
            implicitHeight: 32
            implicitWidth: 240
            color: styledItem.highlighted ? Theme.surface : "transparent"
            radius: Theme.radiusSmall
        }
    }

    component StyledSeparator: MenuSeparator {
        contentItem: Rectangle {
            implicitHeight: 1
            color: Theme.overlay
        }
        background: Rectangle { color: "transparent" }
    }

    background: Rectangle {
        implicitWidth: 260
        color: Theme.crust
        radius: Theme.radiusMedium
        border.color: Theme.overlay
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
