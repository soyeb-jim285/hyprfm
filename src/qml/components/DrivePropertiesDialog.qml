import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root

    property var drive: ({})
    title: qsTr("Drive properties")
    dialogWidth: Math.min(width - 40, Math.round(460 * Theme.uiScale))
    closeOnOverlayPress: false
    closeOnEscape: false
    initialFocusItem: closeButton

    component SelectableText: TextEdit {
        readOnly: true
        selectByMouse: true
        textFormat: TextEdit.PlainText
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.base
        font.pointSize: Theme.fontNormal

        // Give the focused text the standard Copy and Select All shortcuts.
        Keys.onShortcutOverride: event => {
            if (event.matches(StandardKey.Copy) || event.matches(StandardKey.SelectAll))
                event.accepted = true
        }
    }

    Repeater {
        model: [
            { key: "name", label: qsTr("Name"), value: root.drive.deviceName },
            { key: "status", label: qsTr("Status"), value: root.drive.mounted ? qsTr("Mounted")
                : root.drive.encrypted ? qsTr("Locked")
                : root.drive.backend === "mtp-missing" ? qsTr("Unavailable") : qsTr("Not mounted") },
            { key: "device", label: qsTr("Device"), value: root.drive.devicePath },
            { key: "filesystem", label: qsTr("File system"), value: root.drive.fileSystemType },
            { key: "mount", label: qsTr("Mount point"), value: root.drive.mountPoint },
            { key: "capacity", label: qsTr("Capacity"), value: root.drive.deviceSize > 0
                ? Q.Format.bytes(root.drive.deviceSize) : qsTr("Unavailable") },
            { key: "used", label: qsTr("Used"), value: root.drive.capacityKnown
                ? Q.Format.bytes(root.drive.totalSize - root.drive.freeSpace) : "" },
            { key: "available", label: qsTr("Available"), value: root.drive.capacityKnown
                ? Q.Format.bytes(root.drive.freeSpace) : "" }
        ].filter(row => row.value)

        delegate: RowLayout {
            required property var modelData

            Layout.fillWidth: true
            spacing: 12

            SelectableText {
                Layout.preferredWidth: 106 * Theme.uiScale
                Layout.alignment: Qt.AlignTop
                text: modelData.label
                color: Theme.subtext
                wrapMode: TextEdit.WordWrap
            }

            SelectableText {
                objectName: "driveProperty-" + modelData.key
                Layout.fillWidth: true
                activeFocusOnTab: true
                Accessible.name: modelData.label
                text: modelData.value
                wrapMode: TextEdit.WrapAnywhere
            }
        }
    }

    Q.Button {
        id: closeButton
        objectName: "drivePropertiesCloseButton"
        Layout.alignment: Qt.AlignRight
        text: qsTr("Close")
        size: "small"
        onClicked: root.close()
    }
}
