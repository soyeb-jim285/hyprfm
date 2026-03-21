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

    // Sync fsModel when active tab changes
    Connections {
        target: tabModel
        function onActiveIndexChanged() {
            if (tabModel.activeTab)
                fsModel.setRootPath(tabModel.activeTab.currentPath)
        }
    }

    // Sync fsModel when active tab navigates
    Connections {
        target: tabModel.activeTab
        function onCurrentPathChanged() {
            if (tabModel.activeTab)
                fsModel.setRootPath(tabModel.activeTab.currentPath)
        }
    }

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

            // File view
            FileViewContainer {
                Layout.fillWidth: true
                Layout.fillHeight: true
                fsModel: fsModel
                viewMode: tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"

                onFileActivated: (filePath, isDirectory) => {
                    if (isDirectory) {
                        if (tabModel.activeTab) tabModel.activeTab.navigateTo(filePath)
                    } else {
                        fileOps.openFile(filePath)
                    }
                }
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
