import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: "HyprFM"
    color: Theme.base

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Tab bar placeholder
        Rectangle {
            Layout.fillWidth: true
            height: 40
            color: Theme.mantle
        }

        // Toolbar placeholder
        Rectangle {
            Layout.fillWidth: true
            height: 44
            color: Theme.base
        }

        // Main content area
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Sidebar placeholder
            Rectangle {
                width: config.sidebarWidth
                Layout.fillHeight: true
                color: Theme.mantle
                visible: config.sidebarVisible
            }

            // File view placeholder
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.base
            }
        }

        // Status bar
        StatusBar {
            Layout.fillWidth: true
            itemCount: fsModel.fileCount + fsModel.folderCount
            folderCount: fsModel.folderCount
        }
    }
}
