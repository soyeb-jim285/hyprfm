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

        // Tab bar
        FileTabBar {
            Layout.fillWidth: true
        }

        // Toolbar with breadcrumb and view mode toggle
        Toolbar {
            Layout.fillWidth: true
            activeTab: tabModel.activeTab
        }

        // Main content area
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Sidebar with bookmarks
            Sidebar {
                width: config.sidebarWidth
                Layout.fillHeight: true
                visible: config.sidebarVisible
                currentPath: tabModel.activeTab ? tabModel.activeTab.currentPath : ""
                onBookmarkClicked: (path) => {
                    if (tabModel.activeTab) tabModel.activeTab.navigateTo(path)
                }
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
