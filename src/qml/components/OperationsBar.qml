import QtQuick
import QtQuick.Layouts
import HyprFM
import "../quill" as Quill

Rectangle {
    id: root

    visible: fileOps.busy
    height: visible ? 48 : 0
    color: Theme.crust

    Behavior on height {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        spacing: 4

        Text {
            Layout.fillWidth: true
            text: fileOps.statusText
            color: Theme.subtext
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideMiddle
        }

        Quill.ProgressBar {
            Layout.fillWidth: true
            value: fileOps.progress
        }
    }
}
