import QtQuick
import HyprFM

Item {
    id: root

    // "grid" | "detailed" | "miller" | "compact"
    property string viewMode: "grid"
    property var fileModel: null
    property string currentPath: ""
    property bool showComputer: false
    // Why the folder could not be listed; shown instead of an empty view (#74).
    property string loadError: ""
    // How much wider this view will be once a running pane animation ends
    // (negative when it is shrinking). The grid is laid out at that width
    // and clipped by the pane, so its columns do not reflow every frame.
    property real settledWidthOffset: 0

    signal fileActivated(string filePath, bool isDirectory)
    signal openInNewTabRequested(string path)
    signal openInSplitViewRequested(string path)
    signal drivePropertiesRequested(var drive)
    signal featureHintRequested(string message)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)
    signal selectionChanged()
    signal interactionStarted()
    signal transferRequested(var paths, string destinationPath, bool moveOperation)
    signal sortRequested(string column, bool ascending)

    // Each view is built the first time it is shown and kept after that, so
    // switching back is instant without building unseen views at startup.
    // Null until built.
    readonly property var gridViewItem: gridLoader.item
    readonly property var detailedViewItem: detailedLoader.item
    readonly property var millerViewItem: millerLoader.item
    readonly property var compactViewItem: compactLoader.item
    readonly property var computerViewItem: computerLoader.item

    function clamp(value, lo, hi) {
        return Math.max(lo, Math.min(hi, value))
    }

    // Zoom is persisted in sessionState. A view takes the saved value when it
    // is built and writes back what it actually accepted, since views clamp:
    // an out-of-range or negative value in session.json would otherwise be
    // shown corrected but never fixed on disk. Values of 0 mean "nothing
    // saved", so the view keeps its own default. After that the views follow
    // sessionState (the settings panel writes it directly) and feed it; this
    // cannot ping-pong, because assigning a value a view already holds emits
    // nothing and the clamp is idempotent. It also keeps both split panes at
    // the same zoom, which is what the single saved value always meant.
    function applyZoom() {
        const grid = root.gridViewItem
        const detailed = root.detailedViewItem
        const miller = root.millerViewItem
        const compact = root.compactViewItem
        if (compact && sessionState.rowHeightCompact > 0)
            compact.rowHeight = clamp(sessionState.rowHeightCompact, compact.minRowHeight, compact.maxRowHeight)
        if (grid && sessionState.gridColumns > 0)
            grid.columnCount = clamp(sessionState.gridColumns, grid.minColumns, grid.maxColumns)
        if (detailed && sessionState.rowHeightDetailed > 0)
            detailed.rowHeight = clamp(sessionState.rowHeightDetailed,
                                       detailed.minRowHeight, detailed.maxRowHeight)
        if (miller && sessionState.rowHeightMiller > 0)
            miller.rowHeight = clamp(sessionState.rowHeightMiller,
                                     miller.minRowHeight, miller.maxRowHeight)
    }

    Connections {
        target: sessionState
        function onGridColumnsChanged() { root.applyZoom() }
        function onRowHeightDetailedChanged() { root.applyZoom() }
        function onRowHeightMillerChanged() { root.applyZoom() }
        function onRowHeightCompactChanged() { root.applyZoom() }
    }

    function activeView() {
        if (showComputer) return computerViewItem
        if (viewMode === "grid") return gridViewItem
        if (viewMode === "miller") return millerViewItem
        if (viewMode === "compact") return compactViewItem
        return detailedViewItem
    }

    function selectAll() {
        const view = activeView()
        if (view)
            view.selectAll()
    }

    // Only a shown view has a model, and focusPath ignores a view without
    // one, so this only ever reaches the view on screen.
    function focusPath(path, reveal) {
        const view = activeView()
        if (view)
            view.focusPath(path, reveal)
    }

    Loader {
        id: gridLoader
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.max(0, parent.width + root.settledWidthOffset)
        active: root.viewMode === "grid" || item !== null
        sourceComponent: Component {
            FileGridView {
                property bool zoomReady: false
                visible: !root.showComputer && root.viewMode === "grid"
                model: visible ? root.fileModel : null
                currentPath: root.currentPath

                onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
                onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
                onSelectedIndicesChanged: root.selectionChanged()
                onInteractionStarted: root.interactionStarted()
                onTransferRequested: (paths, destinationPath, moveOperation) => root.transferRequested(paths, destinationPath, moveOperation)
                onColumnCountChanged: if (zoomReady) sessionState.gridColumns = columnCount

                Component.onCompleted: {
                    if (sessionState.gridColumns > 0)
                        columnCount = root.clamp(sessionState.gridColumns, minColumns, maxColumns)
                    sessionState.gridColumns = columnCount
                    zoomReady = true
                }
            }
        }
    }

    Loader {
        id: detailedLoader
        anchors.fill: parent
        active: root.viewMode === "detailed" || item !== null
        sourceComponent: Component {
            FileDetailedView {
                property bool zoomReady: false
                visible: !root.showComputer && root.viewMode === "detailed"
                viewModel: visible ? root.fileModel : null
                currentPath: root.currentPath

                onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
                onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
                onSortRequested: (col, asc) => root.sortRequested(col, asc)
                onSelectedIndicesChanged: root.selectionChanged()
                onInteractionStarted: root.interactionStarted()
                onTransferRequested: (paths, destinationPath, moveOperation) => root.transferRequested(paths, destinationPath, moveOperation)
                onRowHeightChanged: if (zoomReady) sessionState.rowHeightDetailed = rowHeight

                Component.onCompleted: {
                    if (sessionState.rowHeightDetailed > 0)
                        rowHeight = root.clamp(sessionState.rowHeightDetailed, minRowHeight, maxRowHeight)
                    sessionState.rowHeightDetailed = rowHeight
                    zoomReady = true
                }
            }
        }
    }

    Loader {
        id: millerLoader
        anchors.fill: parent
        active: root.viewMode === "miller" || item !== null
        sourceComponent: Component {
            FileMillerView {
                property bool zoomReady: false
                visible: !root.showComputer && root.viewMode === "miller"
                fileModel: visible ? root.fileModel : null
                currentPath: root.currentPath

                onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
                onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
                onSelectionChanged: root.selectionChanged()
                onInteractionStarted: root.interactionStarted()
                onTransferRequested: (paths, destinationPath, moveOperation) => root.transferRequested(paths, destinationPath, moveOperation)
                onRowHeightChanged: if (zoomReady) sessionState.rowHeightMiller = rowHeight

                Component.onCompleted: {
                    if (sessionState.rowHeightMiller > 0)
                        rowHeight = root.clamp(sessionState.rowHeightMiller, minRowHeight, maxRowHeight)
                    sessionState.rowHeightMiller = rowHeight
                    zoomReady = true
                }
            }
        }
    }

    Loader {
        id: compactLoader
        anchors.fill: parent
        active: root.viewMode === "compact" || item !== null
        sourceComponent: Component {
            FileCompactView {
                property bool zoomReady: false
                visible: !root.showComputer && root.viewMode === "compact"
                model: visible ? root.fileModel : null
                currentPath: root.currentPath

                onFileActivated: (fp, isDir) => root.fileActivated(fp, isDir)
                onContextMenuRequested: (fp, isDir, pos) => root.contextMenuRequested(fp, isDir, pos)
                onSelectedIndicesChanged: root.selectionChanged()
                onInteractionStarted: root.interactionStarted()
                onTransferRequested: (paths, destinationPath, moveOperation) => root.transferRequested(paths, destinationPath, moveOperation)
                onRowHeightChanged: if (zoomReady) sessionState.rowHeightCompact = rowHeight

                Component.onCompleted: {
                    if (sessionState.rowHeightCompact > 0)
                        rowHeight = root.clamp(sessionState.rowHeightCompact, minRowHeight, maxRowHeight)
                    sessionState.rowHeightCompact = rowHeight
                    zoomReady = true
                }
            }
        }
    }

    Loader {
        id: computerLoader
        anchors.fill: parent
        active: root.showComputer || item !== null
        sourceComponent: Component {
            ComputerView {
                objectName: "computerView"
                visible: root.showComputer
                viewMode: root.viewMode
                onFileActivated: (path, isDir) => root.fileActivated(path, isDir)
                onOpenInNewTabRequested: path => root.openInNewTabRequested(path)
                onOpenInSplitViewRequested: path => root.openInSplitViewRequested(path)
                onDrivePropertiesRequested: drive => root.drivePropertiesRequested(drive)
                onFeatureHintRequested: message => root.featureHintRequested(message)
                onInteractionStarted: root.interactionStarted()
                onSelectionChanged: root.selectionChanged()
            }
        }
    }

    Text {
        objectName: "loadErrorText"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        visible: !root.showComputer && root.loadError !== ""
        text: root.loadError
        color: Theme.subtext
        font.pointSize: Theme.fontNormal
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }
}
