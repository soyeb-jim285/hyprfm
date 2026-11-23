import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root

    property var drive: ({})
    title: qsTr("Drive properties")
    subtitle: drive.deviceName || ""
    dialogWidth: Math.min(width - 40, Math.round(460 * Theme.uiScale))
    initialFocusItem: closeButton

    Repeater {
        model: [
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

            Text {
                Layout.preferredWidth: 106 * Theme.uiScale
                Layout.alignment: Qt.AlignTop
                text: modelData.label
                color: Theme.subtext
                font.pointSize: Theme.fontNormal
                wrapMode: Text.WordWrap
            }

            Text {
                objectName: "driveProperty-" + modelData.key
                Layout.fillWidth: true
                text: modelData.value
                textFormat: Text.PlainText
                color: Theme.text
                font.pointSize: Theme.fontNormal
                wrapMode: Text.WrapAnywhere
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
