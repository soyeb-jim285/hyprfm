import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: statusBar

    property int itemCount: 0
    property int folderCount: 0
    property int selectedCount: 0
    property string selectedSize: ""

    height: 28
    color: Theme.mantle

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing

        Text {
            Layout.fillWidth: true
            text: {
                const files = statusBar.itemCount - statusBar.folderCount
                return statusBar.itemCount + " items (" + statusBar.folderCount + " folders, " + files + " files)"
            }
            color: Theme.subtext
            font.pixelSize: Theme.fontSmall
            verticalAlignment: Text.AlignVCenter
        }

        Text {
            visible: statusBar.selectedCount > 0
            text: statusBar.selectedCount + " selected" + (statusBar.selectedSize ? " \u2014 " + statusBar.selectedSize : "")
            color: Theme.subtext
            font.pixelSize: Theme.fontSmall
            verticalAlignment: Text.AlignVCenter
        }
    }
}
