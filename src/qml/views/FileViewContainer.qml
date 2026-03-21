import QtQuick
import HyprFM

Item {
    id: root

    // "grid" | "list" | "detailed"
    property string viewMode: "grid"
    property var fsModel: null
    property string currentPath: ""

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)
    signal selectionChanged()

    // Expose sub-views so main.qml can access selection state
    property alias gridViewItem: gridView
    property alias listViewItem: listView
    property alias detailedViewItem: detailedView

    // Recompute rootIndex whenever fsModel or its rootPath changes
    property var currentRootIndex: fsModel ? fsModel.rootIndex() : null

    Connections {
        target: fsModel
        function onRootPathChanged() {
            root.currentRootIndex = root.fsModel ? root.fsModel.rootIndex() : null
        }
    }

    FileGridView {
        id: gridView
        anchors.fill: parent
        visible: root.viewMode === "grid"
        model: root.fsModel
        rootIndex: root.currentRootIndex
        currentPath: root.currentPath

        onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
        onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
        onSelectedIndicesChanged: root.selectionChanged()
    }

    FileListView {
        id: listView
        anchors.fill: parent
        visible: root.viewMode === "list"
        model: root.fsModel
        rootIndex: root.currentRootIndex
        currentPath: root.currentPath

        onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
        onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
        onSelectedIndicesChanged: root.selectionChanged()
    }

    FileDetailedView {
        id: detailedView
        anchors.fill: parent
        visible: root.viewMode === "detailed"
        viewModel: root.fsModel
        viewRootIndex: root.currentRootIndex
        currentPath: root.currentPath

        onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
        onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
        onSortRequested: (col, asc) => {
            if (root.fsModel) root.fsModel.sortByColumn(col, asc)
        }
        onSelectedIndicesChanged: root.selectionChanged()
    }
}
