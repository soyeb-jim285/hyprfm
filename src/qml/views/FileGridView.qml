import QtQuick
import QtQuick.Controls
import QtQml.Models
import HyprFM

GridView {
    id: root

    property var fsModel: null
    property var fsRootIndex: undefined

    property var selectedIndices: []
    property int lastSelectedIndex: -1

    // Current directory path (used as drop target)
    property string currentPath: ""

    signal fileActivated(string filePath, bool isDirectory)
    signal contextMenuRequested(string filePath, bool isDirectory, point position)

    clip: true
    cellWidth: 110
    cellHeight: 110

    // Elastic overscroll
    boundsMovement: Flickable.FollowBoundsBehavior
    boundsBehavior: Flickable.DragAndOvershootBounds
    flickDeceleration: 1500
    maximumFlickVelocity: 2500

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
    }

    function selectIndex(idx, ctrl, shift) {
        if (shift && lastSelectedIndex >= 0) {
            // Range select
            var lo = Math.min(idx, lastSelectedIndex)
            var hi = Math.max(idx, lastSelectedIndex)
            var newSel = ctrl ? selectedIndices.slice() : []
            for (var i = lo; i <= hi; i++) {
                if (newSel.indexOf(i) < 0) newSel.push(i)
            }
            selectedIndices = newSel
        } else if (ctrl) {
            // Toggle
            var newSel2 = selectedIndices.slice()
            var pos = newSel2.indexOf(idx)
            if (pos >= 0)
                newSel2.splice(pos, 1)
            else
                newSel2.push(idx)
            selectedIndices = newSel2
            lastSelectedIndex = idx
        } else {
            // Single select
            selectedIndices = [idx]
            lastSelectedIndex = idx
        }
    }

    function clearSelection() {
        selectedIndices = []
        lastSelectedIndex = -1
    }

    // Helper: collect file:// URIs for the current selection (or given path)
    function uriListForPaths(paths) {
        return paths.map(function(p) { return "file://" + p }).join("\n")
    }

    model: DelegateModel {
        model: root.fsModel
        rootIndex: root.fsRootIndex

        delegate: Item {
        id: delegateItem
        width: root.cellWidth
        height: root.cellHeight

        required property int index
        required property string fileName
        required property string filePath
        required property bool isDir

        readonly property bool isSelected: root.selectedIndices.indexOf(index) >= 0

        // ── Drag support ─────────────────────────────────────────────────────
        Drag.active: dragHandler.active
        Drag.mimeData: {
            var paths = root.selectedIndices.length > 1 && root.isSelected
                ? root.selectedIndices.map(function(i) {
                    var mi = fsModel.index(i, 0, fsModel.rootIndex())
                    return "file://" + fsModel.filePath(mi)
                  })
                : ["file://" + delegateItem.filePath]
            return { "text/uri-list": paths.join("\n") }
        }
        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
        Drag.dragType: Drag.Automatic

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            radius: Theme.radiusMedium
            opacity: delegateItem.Drag.active ? 0.5 : 1.0
            color: {
                if (delegateItem.isSelected)
                    return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                if (ma.containsMouse)
                    return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)
                return "transparent"
            }
            border.color: delegateItem.isSelected ? Theme.accent : "transparent"
            border.width: delegateItem.isSelected ? 2 : 0

            Column {
                anchors.centerIn: parent
                spacing: 6

                // Show thumbnail for image files, emoji icon otherwise
                readonly property bool isImage: !delegateItem.isDir &&
                    /\.(png|jpg|jpeg|gif|webp|bmp)$/i.test(delegateItem.filePath)

                Image {
                    visible: parent.isImage
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 60
                    height: 60
                    fillMode: Image.PreserveAspectFit
                    source: parent.isImage
                        ? ("image://thumbnail/" + delegateItem.filePath)
                        : ""
                    asynchronous: true
                }

                Text {
                    visible: !parent.isImage
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: delegateItem.isDir ? "📁" : "📄"
                    font.pixelSize: 36
                }

                Text {
                    width: root.cellWidth - 16
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: delegateItem.fileName
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    maximumLineCount: 2
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
            }

            MouseArea {
                id: ma
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                drag.target: delegateItem
                drag.threshold: 8

                onClicked: (mouse) => {
                    if (mouse.button === Qt.RightButton) {
                        root.contextMenuRequested(
                            delegateItem.filePath,
                            delegateItem.isDir,
                            Qt.point(mouse.x + delegateItem.x, mouse.y + delegateItem.y)
                        )
                        return
                    }
                    root.selectIndex(
                        delegateItem.index,
                        mouse.modifiers & Qt.ControlModifier,
                        mouse.modifiers & Qt.ShiftModifier
                    )
                }

                onDoubleClicked: (mouse) => {
                    if (mouse.button !== Qt.LeftButton) return
                    root.fileActivated(delegateItem.filePath, delegateItem.isDir)
                }
            }

            // DragHandler for initiating drag with the Drag attached property
            DragHandler {
                id: dragHandler
                onActiveChanged: {
                    if (active) {
                        // Make sure this item is selected
                        if (!delegateItem.isSelected)
                            root.selectIndex(delegateItem.index, false, false)
                        delegateItem.Drag.start()
                    } else {
                        delegateItem.Drag.drop()
                        // Reset position
                        delegateItem.x = 0
                        delegateItem.y = 0
                    }
                }
            }
        }
    }
    } // end DelegateModel

    // ── Drop area: accept files dropped onto this view ───────────────────────
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        z: -2

        onDropped: (drop) => {
            if (!root.currentPath) return
            var urls = drop.urls
            var paths = []
            for (var i = 0; i < urls.length; i++) {
                var s = urls[i].toString()
                if (s.startsWith("file://"))
                    paths.push(s.substring(7))
            }
            if (paths.length === 0) return
            if (drop.proposedAction === Qt.MoveAction)
                fileOps.moveFiles(paths, root.currentPath)
            else
                fileOps.copyFiles(paths, root.currentPath)
            drop.acceptProposedAction()
        }
    }

    // ── Rubber-band selection ────────────────────────────────────────────────
    // Background MouseArea captures drags on empty space
    MouseArea {
        id: bgMa
        anchors.fill: parent
        z: -1
        acceptedButtons: Qt.LeftButton

        property point dragStart

        onClicked: (mouse) => {
            if (!rubberBand.visible)
                root.clearSelection()
        }

        onPressed: (mouse) => {
            dragStart = Qt.point(mouse.x, mouse.y)
            rubberBand.begin(dragStart)
        }

        onPositionChanged: (mouse) => {
            if (pressed) {
                rubberBand.update(Qt.point(mouse.x, mouse.y))
                // Select intersecting items
                selectIntersecting()
            }
        }

        onReleased: {
            rubberBand.end()
        }

        function selectIntersecting() {
            var rb = rubberBand.selectionRect
            if (rb.width < 4 && rb.height < 4) return

            var newSel = []
            var count = root.count
            for (var i = 0; i < count; i++) {
                var item = root.itemAtIndex(i)
                if (!item) continue
                // Map item rect to view coordinates
                var itemPos = root.mapFromItem(item, 0, 0)
                var itemRect = Qt.rect(itemPos.x, itemPos.y, item.width, item.height)
                if (rectsIntersect(rb, itemRect))
                    newSel.push(i)
            }
            root.selectedIndices = newSel
        }

        function rectsIntersect(a, b) {
            return a.x < b.x + b.width  &&
                   a.x + a.width  > b.x &&
                   a.y < b.y + b.height &&
                   a.y + a.height > b.y
        }
    }

    // Rubber-band visual
    RubberBand {
        id: rubberBand
        anchors.fill: parent
        z: 1
    }
}
