import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root
    Accessible.role: Accessible.ToolBar
    Accessible.name: "Navigation toolbar"

    property var activeTab: null
    property string navigationPath: ""
    property bool canGoBack: false
    property bool canGoForward: false
    property bool splitViewEnabled: false
    property bool isRecentsView: false
    property bool isTrashView: false
    property bool isRemoteView: false
    property bool searchMode: false
    property bool showWindowControls: false
    property string windowButtonLayout: ":minimize,maximize,close"
    property var window: null
    property string currentSearchQuery: ""
    property string searchTypeFilter: ""
    property string searchDateFilter: ""
    property string searchSizeFilter: ""
    property bool filterPanelOpen: false
    property alias searchBar: searchBarLoader.item
    property alias filterPanel: filterPanelLoader.item

    function startEditing() {
        if (!searchMode) breadcrumb.startEditing()
    }

    function syncSearchBarState() {
        if (searchBarLoader.item)
            searchBarLoader.item.applyQuery(currentSearchQuery)
    }

    function syncFilterPanelState() {
        if (!filterPanelLoader.item)
            return

        filterPanelLoader.item.visible = filterPanelOpen
        filterPanelLoader.item.applyState(searchTypeFilter, searchDateFilter, searchSizeFilter)
    }

    onCurrentSearchQueryChanged: syncSearchBarState()
    onSearchTypeFilterChanged: syncFilterPanelState()
    onSearchDateFilterChanged: syncFilterPanelState()
    onSearchSizeFilterChanged: syncFilterPanelState()
    onFilterPanelOpenChanged: syncFilterPanelState()

    signal searchClicked()
    signal connectRemoteRequested()
    signal homeClicked()
    signal searchQueryChanged(string query)
    signal searchFilterToggled()
    signal searchClosed()
    signal searchEnterPressed()
    signal searchNavigateDown()
    signal backRequested()
    signal forwardRequested()
    signal upRequested()
    signal navigateRequested(string targetPath)
    signal splitViewToggled()
    signal typeFilterChanged(string filter)
    signal dateFilterChanged(string filter)
    signal sizeFilterChanged(string filter)
    signal clearAllFilters()
    signal restoreTrashRequested()
    signal emptyTrashRequested()
    signal settingsRequested()
    signal closeRequested()
    signal minimizeRequested()
    signal maximizeRequested()
    signal transferRequested(var paths, string destinationPath, bool moveOperation)

    // Parse "buttons_left:buttons_right" layout string
    readonly property var _parsedLayout: {
        var layout = windowButtonLayout || ":minimize,maximize,close"
        var parts = layout.split(":")
        var leftStr = parts[0] || ""
        var rightStr = parts.length > 1 ? parts[1] : ""
        return {
            left: leftStr ? leftStr.split(",").filter(function(s) { return s.trim() !== "" }) : [],
            right: rightStr ? rightStr.split(",").filter(function(s) { return s.trim() !== "" }) : []
        }
    }

    implicitHeight: toolbarColumn.implicitHeight
    color: Theme.mantle

    DragHandler {
        enabled: root.showWindowControls && root.window
        target: null
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active && root.window && root.window.startSystemMove)
                root.window.startSystemMove()
        }
    }

    ColumnLayout {
        id: toolbarColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        // ── Row 1: Navigation + Breadcrumb + Search ──
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.toolbarRowHeight

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing
                anchors.rightMargin: Theme.spacing
                spacing: 4

                // Left-side window controls
                Repeater {
                    model: root.showWindowControls ? root._parsedLayout.left : []
                    delegate: HoverRect {
                        required property string modelData
                        width: Theme.controlSize; height: Theme.controlSize
                        color: modelData === "close" && hovered
                            ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.9)
                            : (hovered ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent")
                        onClicked: {
                            if (modelData === "close") root.closeRequested()
                            else if (modelData === "minimize") root.minimizeRequested()
                            else if (modelData === "maximize") root.maximizeRequested()
                        }
                        IconX { anchors.centerIn: parent; size: 14; color: parent.modelData === "close" && parent.hovered ? Theme.base : Theme.text; visible: parent.modelData === "close" }
                        IconMinus { anchors.centerIn: parent; size: 14; color: Theme.text; visible: parent.modelData === "minimize" }
                        IconSquare { anchors.centerIn: parent; size: 12; color: Theme.text; visible: parent.modelData === "maximize" }
                    }
                }

                Item {
                    visible: root.showWindowControls && root._parsedLayout.left.length > 0
                    width: visible ? 4 : 0
                    height: 1
                }

                // Back button
                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    hoverEnabled: root.canGoBack
                    opacity: hoverEnabled ? 1.0 : 0.4
                    onClicked: root.backRequested()
                    IconChevronLeft { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                // Forward button
                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    hoverEnabled: root.canGoForward
                    opacity: hoverEnabled ? 1.0 : 0.4
                    onClicked: root.forwardRequested()
                    IconChevronRight { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                // Up button
                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    hoverEnabled: !root.isRecentsView
                    opacity: hoverEnabled ? 1.0 : 0.4
                    onClicked: root.upRequested()
                    IconChevronUp { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                // Breadcrumb / address bar (hidden in search mode)
                Breadcrumb {
                    id: breadcrumb
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !root.searchMode
                    path: root.navigationPath
                    activeTab: root.activeTab
                    isRecentsView: root.isRecentsView
                    onNavigateRequested: (targetPath) => root.navigateRequested(targetPath)
                }

                // Search bar (shown in search mode)
                Loader {
                    id: searchBarLoader
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.compactControlSize
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.searchMode
                    active: root.searchMode
                    sourceComponent: SearchBar {
                        searchQuery: root.currentSearchQuery
                        filterPanelOpen: root.filterPanelOpen
                        onQueryChanged: (query) => root.searchQueryChanged(query)
                        onFilterToggled: root.searchFilterToggled()
                        onSearchClosed: root.searchClosed()
                        onEnterPressed: root.searchEnterPressed()
                        onNavigateDown: root.searchNavigateDown()
                    }
                    onLoaded: {
                        root.syncSearchBarState()
                        item.focusInput()
                    }
                }

                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    visible: !root.searchMode
                    border.width: root.splitViewEnabled ? 1 : 0
                    border.color: root.splitViewEnabled
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.65)
                        : "transparent"
                    color: root.splitViewEnabled
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                  hovered ? 0.30 : 0.22)
                        : (hovered
                            ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                            : "transparent")
                    onClicked: root.splitViewToggled()
                    IconSquareSplitHorizontal {
                        anchors.centerIn: parent
                        size: 18
                        color: root.splitViewEnabled ? Theme.accent : Theme.text
                    }
                }

                // Restore button (only in trash view)
                HoverRect {
                    width: restoreTrashRow.implicitWidth + 16; height: Theme.controlSize
                    visible: root.isTrashView && !root.searchMode
                    onClicked: root.restoreTrashRequested()
                    Row {
                        id: restoreTrashRow
                        anchors.centerIn: parent
                        spacing: 6
                        IconUndo { anchors.verticalCenter: parent.verticalCenter; size: 16; color: Theme.accent }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Restore"
                            color: Theme.text
                            font.pointSize: Theme.fontNormal
                            font.weight: Font.Medium
                        }
                    }
                }

                // Empty Trash button (only in trash view)
                HoverRect {
                    width: emptyTrashRow.implicitWidth + 16; height: Theme.controlSize
                    visible: root.isTrashView && !root.searchMode
                    onClicked: root.emptyTrashRequested()
                    Row {
                        id: emptyTrashRow
                        anchors.centerIn: parent
                        spacing: 6
                        IconTrash { anchors.verticalCenter: parent.verticalCenter; size: 16; color: Theme.error }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "Empty Trash"
                            color: Theme.error
                            font.pointSize: Theme.fontNormal
                            font.weight: Font.Medium
                        }
                    }
                }

                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    visible: !root.searchMode && !root.isTrashView && !root.isRemoteView
                    onClicked: root.searchClicked()
                    IconSearch { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    visible: !root.searchMode
                    onClicked: root.settingsRequested()
                    IconSettings { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                Item {
                    visible: root.showWindowControls && root._parsedLayout.right.length > 0
                    width: visible ? 4 : 0
                    height: 1
                }

                // Right-side window controls
                Repeater {
                    model: root.showWindowControls ? root._parsedLayout.right : []
                    delegate: HoverRect {
                        required property string modelData
                        width: Theme.controlSize; height: Theme.controlSize
                        color: modelData === "close" && hovered
                            ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.9)
                            : (hovered ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent")
                        onClicked: {
                            if (modelData === "close") root.closeRequested()
                            else if (modelData === "minimize") root.minimizeRequested()
                            else if (modelData === "maximize") root.maximizeRequested()
                        }
                        IconX { anchors.centerIn: parent; size: 14; color: parent.modelData === "close" && parent.hovered ? Theme.base : Theme.text; visible: parent.modelData === "close" }
                        IconMinus { anchors.centerIn: parent; size: 14; color: Theme.text; visible: parent.modelData === "minimize" }
                        IconSquare { anchors.centerIn: parent; size: 12; color: Theme.text; visible: parent.modelData === "maximize" }
                    }
                }
            }
        }

        // ── Filter panel (slides in when toggled) ──
        Item {
            Layout.fillWidth: true
                Layout.preferredHeight: root.searchMode && filterPanelLoader.item && filterPanelLoader.item.visible
                    ? filterPanelLoader.item.implicitHeight : 0
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation { duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve }
            }

            Loader {
                id: filterPanelLoader
                anchors.fill: parent
                active: root.searchMode
                sourceComponent: FilterPanel {
                    visible: root.filterPanelOpen
                    onTypeFilterChanged: (filter) => root.typeFilterChanged(filter)
                    onDateFilterChanged: (filter) => root.dateFilterChanged(filter)
                    onSizeFilterChanged: (filter) => root.sizeFilterChanged(filter)
                    onClearAllFilters: root.clearAllFilters()
                }
                onLoaded: root.syncFilterPanelState()
            }
        }

        // ── Row 2: Tab bar (only visible with 2+ tabs) ──
        Item {
            id: tabBarRow
            Layout.fillWidth: true
            Layout.preferredHeight: tabModel.count > 1 ? Math.round(36 * Theme.uiScale) : 0
            visible: Layout.preferredHeight > 0 || tabBarHeightAnim.running
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    id: tabBarHeightAnim
                    duration: Theme.animDurationSlow; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve
                }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                // Top separator
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                }

                RowLayout {
                    id: tabRow
                    anchors.fill: parent
                    spacing: 0

                    // Track how many tabs are closing so others can grow immediately
                    property int closingCount: 0
                    property int effectiveCount: Math.max(tabModel.count - closingCount, 1)
                    property int hoveredIndex: -1

                    Repeater {
                        id: tabRepeater
                        model: tabModel

                        delegate: Rectangle {
                            id: tabDelegate

                            required property int index
                            required property var model

                            Layout.fillHeight: true
                            Layout.preferredWidth: closing ? 0 : tabRow.width / tabRow.effectiveCount
                            property bool closing: false

                            Behavior on Layout.preferredWidth {
                                NumberAnimation { duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve }
                            }

                            opacity: 0
                            scale: 0.94

                            property int frozenIndex: -1

                            function startClose() {
                                if (closing) return
                                frozenIndex = tabDelegate.index
                                closing = true
                                tabRow.closingCount++
                                exitAnim.start()
                            }

                            Component.onCompleted: enterAnim.start()

                            ParallelAnimation {
                                id: enterAnim
                                NumberAnimation {
                                    target: tabDelegate; property: "opacity"
                                    from: 0; to: 1; duration: Theme.animDuration
                                    easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve
                                }
                                NumberAnimation {
                                    target: tabDelegate; property: "scale"
                                    from: 0.88; to: 1; duration: Theme.animDurationSlow
                                    easing.type: Easing.OutBack; easing.overshoot: 0.5
                                }
                            }

                            color: "transparent"

                            // Drop area on tab
                            DropArea {
                                id: tabDropArea
                                anchors.fill: parent
                                keys: ["text/uri-list"]

                                onDropped: (drop) => {
                                    var destPath = tabDelegate.model.path
                                    if (!destPath) return
                                    var urls = drop.urls
                                    var paths = []
                                    for (var i = 0; i < urls.length; i++) {
                                        var s = urls[i].toString()
                                        paths.push(s.startsWith("file://") ? decodeURIComponent(s.substring(7)) : s)
                                    }
                                    if (paths.length === 0) return
                                    // Don't move files into the directory they're already in
                                    var allSameDir = paths.every(function(p) {
                                        var parentDir = p.substring(0, p.lastIndexOf("/"))
                                        return parentDir === destPath
                                    })
                                    if (allSameDir) return
                                    root.transferRequested(paths, destPath, drop.proposedAction === Qt.MoveAction)
                                    drop.acceptProposedAction()
                                }
                            }

                            SequentialAnimation {
                                id: exitAnim
                                ParallelAnimation {
                                    NumberAnimation {
                                        target: tabDelegate; property: "opacity"
                                        to: 0; duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve
                                    }
                                    NumberAnimation {
                                        target: tabDelegate; property: "scale"
                                        to: 0.88; duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve
                                    }
                                }
                                ScriptAction {
                                    script: {
                                        tabRow.closingCount = Math.max(tabRow.closingCount - 1, 0)
                                        tabModel.closeTab(tabDelegate.frozenIndex)
                                    }
                                }
                            }

                            // Separator between tabs
                            Rectangle {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 1
                                height: parent.height * 0.5
                                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
                                visible: tabDelegate.index < tabModel.count - 1
                                opacity: (tabDelegate.index === tabModel.activeIndex
                                    || tabDelegate.index + 1 === tabModel.activeIndex
                                    || tabDelegate.index === tabRow.hoveredIndex
                                    || tabDelegate.index + 1 === tabRow.hoveredIndex) ? 0 : 1
                                Behavior on opacity { NumberAnimation { duration: Theme.animDuration } }
                            }

                            HoverHandler {
                                id: tabDelegateHover
                                onHoveredChanged: {
                                    if (hovered) tabRow.hoveredIndex = tabDelegate.index
                                    else if (tabRow.hoveredIndex === tabDelegate.index) tabRow.hoveredIndex = -1
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.MiddleButton)
                                        tabDelegate.startClose()
                                    else
                                        tabModel.activeIndex = tabDelegate.index
                                }
                            }

                            // Inner highlight
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 5
                                radius: Theme.radiusSmall
                                color: {
                                    if (tabDelegate.index === tabModel.activeIndex)
                                        return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                                    if (tabDelegateHover.hovered)
                                        return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                                    return "transparent"
                                }
                                Behavior on color { ColorAnimation { duration: Theme.animDuration } }
                                border.width: tabDelegate.index === tabModel.activeIndex ? 1 : 0
                                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                            }

                            // Tab label
                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignHCenter
                                text: tabDelegate.model.title || "New Tab"
                                color: tabDelegate.index === tabModel.activeIndex ? Theme.text : Theme.subtext
                                font.pointSize: Theme.fontNormal
                                font.weight: tabDelegate.index === tabModel.activeIndex ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            // Close button — only visible on hover
                            Rectangle {
                                id: closeBtn
                                width: 20; height: 20; radius: 10
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                visible: tabModel.count > 1 && tabDelegateHover.hovered
                                color: closeHover.hovered
                                    ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.8)
                                    : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                                IconX {
                                    anchors.centerIn: parent; size: 10
                                    color: closeHover.hovered ? Theme.base : Theme.muted
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: tabDelegate.startClose()
                                }

                                HoverHandler {
                                    id: closeHover
                                    cursorShape: Qt.PointingHandCursor
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
