import QtQuick
import QtQuick.Controls
import HyprFM

Menu {
    id: root

    // Properties set by caller before opening
    property string targetPath: ""
    property bool targetIsDir: false
    property bool isEmptySpace: false
    property var selectedPaths: []

    // Signals for each action
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
    signal propertiesRequested(string path)

    // Helpers
    property var effectivePaths: (selectedPaths.length > 0) ? selectedPaths : (targetPath !== "" ? [targetPath] : [])
    property string effectiveDir: {
        if (isEmptySpace) return targetPath
        if (targetIsDir) return targetPath
        // parent directory of a file
        var p = targetPath
        var idx = p.lastIndexOf("/")
        return idx > 0 ? p.substring(0, idx) : "/"
    }

    // ── Background styling ──────────────────────────────────────────────────
    background: Rectangle {
        implicitWidth: 220
        implicitHeight: 36
        color: Theme.mantle
        radius: Theme.radiusMedium
        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
        border.width: 1
    }

    // ── Delegate (item styling) ─────────────────────────────────────────────
    delegate: MenuItem {
        id: menuItem
        contentItem: Text {
            leftPadding: 8
            text: menuItem.text
            font.pixelSize: Theme.fontNormal
            color: menuItem.enabled ? Theme.text : Theme.muted
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            implicitHeight: 32
            color: menuItem.highlighted ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : "transparent"
            radius: Theme.radiusSmall
        }
    }

    // ── Standard items ──────────────────────────────────────────────────────

    MenuItem {
        text: "Open"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.openRequested(root.targetPath)
    }

    MenuSeparator {
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        contentItem: Rectangle {
            implicitHeight: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
        }
    }

    MenuItem {
        text: "Cut"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.cutRequested(root.effectivePaths)
    }

    MenuItem {
        text: "Copy"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.copyRequested(root.effectivePaths)
    }

    MenuItem {
        text: "Paste"
        enabled: clipboard.hasContent
        visible: clipboard.hasContent
        height: visible ? implicitHeight : 0
        onTriggered: root.pasteRequested(root.effectiveDir)
    }

    MenuItem {
        text: "Copy Path"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.copyPathRequested(root.targetPath)
    }

    MenuSeparator {
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        contentItem: Rectangle {
            implicitHeight: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
        }
    }

    MenuItem {
        text: "Rename"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.renameRequested(root.targetPath)
    }

    MenuItem {
        text: "Move to Trash"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.trashRequested(root.effectivePaths)
    }

    MenuItem {
        text: "Delete"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        contentItem: Text {
            leftPadding: 8
            text: "Delete"
            font.pixelSize: Theme.fontNormal
            color: Theme.error
            verticalAlignment: Text.AlignVCenter
        }
        onTriggered: root.deleteRequested(root.effectivePaths)
    }

    MenuSeparator {
        contentItem: Rectangle {
            implicitHeight: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
        }
    }

    MenuItem {
        text: "Open in Terminal"
        onTriggered: root.openInTerminalRequested(root.effectiveDir)
    }

    MenuSeparator {
        contentItem: Rectangle {
            implicitHeight: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
        }
    }

    MenuItem {
        text: "New Folder"
        onTriggered: root.newFolderRequested(root.effectiveDir)
    }

    MenuItem {
        text: "New File"
        onTriggered: root.newFileRequested(root.effectiveDir)
    }

    MenuSeparator {
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        contentItem: Rectangle {
            implicitHeight: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
        }
    }

    MenuItem {
        text: "Properties"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.propertiesRequested(root.targetPath)
    }

    // ── Custom actions from config ──────────────────────────────────────────
    Instantiator {
        model: config.customContextActions
        delegate: MenuItem {
            required property var modelData
            text: modelData.label ?? ""
            onTriggered: {
                if (modelData.command) {
                    Qt.openUrlExternally("exec:" + modelData.command.replace("%f", root.targetPath))
                }
            }
        }
        onObjectAdded: (index, object) => root.addItem(object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
