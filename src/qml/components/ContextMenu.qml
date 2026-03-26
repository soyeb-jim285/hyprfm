import QtQuick
import QtQuick.Controls
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
    signal propertiesRequested(string path)

    property var effectivePaths: (selectedPaths.length > 0) ? selectedPaths : (targetPath !== "" ? [targetPath] : [])
    property string effectiveDir: {
        if (isEmptySpace) return targetPath
        if (targetIsDir) return targetPath
        var p = targetPath
        var idx = p.lastIndexOf("/")
        return idx > 0 ? p.substring(0, idx) : "/"
    }

    // Shared styling component for menu items
    component StyledMenuItem: MenuItem {
        id: styledItem
        property color textColor: Theme.text
        contentItem: Text {
            leftPadding: 12
            rightPadding: 12
            text: styledItem.text
            font.pixelSize: Theme.fontNormal
            color: styledItem.enabled ? styledItem.textColor : Theme.muted
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            implicitHeight: 30
            implicitWidth: 200
            color: styledItem.highlighted ? Theme.surface : "transparent"
            radius: Theme.radiusSmall
        }
    }

    component StyledSeparator: MenuSeparator {
        contentItem: Rectangle {
            implicitHeight: 1
            color: Theme.overlay
        }
        background: Rectangle {
            color: "transparent"
        }
    }

    background: Rectangle {
        implicitWidth: 220
        color: Theme.crust
        radius: Theme.radiusMedium
        border.color: Theme.overlay
        border.width: 1
    }

    StyledMenuItem {
        text: "Open"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.openRequested(root.targetPath)
    }

    StyledSeparator {
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
    }

    StyledMenuItem {
        text: "Cut"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.cutRequested(root.effectivePaths)
    }

    StyledMenuItem {
        text: "Copy"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.copyRequested(root.effectivePaths)
    }

    StyledMenuItem {
        text: "Paste"
        enabled: clipboard.hasContent
        visible: clipboard.hasContent
        height: visible ? implicitHeight : 0
        onTriggered: root.pasteRequested(root.effectiveDir)
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
        text: "Rename"
        visible: !isEmptySpace && targetPath !== ""
        height: visible ? implicitHeight : 0
        onTriggered: root.renameRequested(root.targetPath)
    }

    StyledMenuItem {
        text: "Move to Trash"
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.trashRequested(root.effectivePaths)
    }

    StyledMenuItem {
        text: "Delete"
        textColor: Theme.error
        visible: !isEmptySpace && root.effectivePaths.length > 0
        height: visible ? implicitHeight : 0
        onTriggered: root.deleteRequested(root.effectivePaths)
    }

    StyledSeparator {}

    StyledMenuItem {
        text: "Open in Terminal"
        onTriggered: root.openInTerminalRequested(root.effectiveDir)
    }

    StyledSeparator {}

    StyledMenuItem {
        text: "New Folder"
        onTriggered: root.newFolderRequested(root.effectiveDir)
    }

    StyledMenuItem {
        text: "New File"
        onTriggered: root.newFileRequested(root.effectiveDir)
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

    // Custom actions from config
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
