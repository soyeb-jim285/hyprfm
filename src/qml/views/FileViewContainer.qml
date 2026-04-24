import QtQuick
import HyprFM

Item {
    id: root

    // "grid" | "detailed" | "miller"
    property string viewMode: "grid"
    property var fileModel: null
    property string currentPath: ""

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)
    signal selectionChanged()
    signal interactionStarted()
    signal transferRequested(var paths, string destinationPath, bool moveOperation)

    function selectAll() {
        if (viewMode === "grid") gridView.selectAll()
        else if (viewMode === "miller") millerView.selectAll()
        else detailedView.selectAll()
    }

    function focusPath(path, reveal) {
        gridView.focusPath(path, reveal)
        detailedView.focusPath(path, reveal)
        millerView.focusPath(path, reveal)
    }

    // Expose sub-views so main.qml can access selection state
    property alias gridViewItem: gridView
    property alias detailedViewItem: detailedView
    property alias millerViewItem: millerView

    FileGridView {
        id: gridView
        anchors.fill: parent
        visible: root.viewMode === "grid"
        model: visible ? root.fileModel : null
        currentPath: root.currentPath

        onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
        onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
        onSelectedIndicesChanged: root.selectionChanged()
        onInteractionStarted: root.interactionStarted()
        onTransferRequested: (paths, destinationPath, moveOperation) => root.transferRequested(paths, destinationPath, moveOperation)
    }

    FileDetailedView {
        id: detailedView
        anchors.fill: parent
        visible: root.viewMode === "detailed"
        viewModel: visible ? root.fileModel : null
        currentPath: root.currentPath

        onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
        onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
        onSortRequested: (col, asc) => {
            if (root.fileModel) root.fileModel.sortByColumn(col, asc)
        }
        onSelectedIndicesChanged: root.selectionChanged()
        onInteractionStarted: root.interactionStarted()
        onTransferRequested: (paths, destinationPath, moveOperation) => root.transferRequested(paths, destinationPath, moveOperation)
    }

    FileMillerView {
        id: millerView
        anchors.fill: parent
        visible: root.viewMode === "miller"
        fileModel: visible ? root.fileModel : null
        currentPath: root.currentPath

        onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
        onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
        onSelectionChanged: root.selectionChanged()
        onInteractionStarted: root.interactionStarted()
        onTransferRequested: (paths, destinationPath, moveOperation) => root.transferRequested(paths, destinationPath, moveOperation)
    }
}
