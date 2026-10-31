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
    property bool isSplitHorizontal: false
    property bool isRecentsView: false
    property bool isTrashView: false
    property bool isRemoteView: false
    property bool searchMode: false
    property bool sidebarVisible: true
    property bool sidebarOnRight: false
    property string currentViewMode: "grid"
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
    readonly property bool pathEditing: breadcrumb.editMode

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
    signal sidebarToggleRequested()
    signal backRequested()
    signal forwardRequested()
    signal upRequested()
    signal navigateRequested(string targetPath)
    signal splitRequested(string orientation)
    signal viewModeRequested(string mode)
    signal typeFilterChanged(string filter)
    signal dateFilterChanged(string filter)
    signal sizeFilterChanged(string filter)
    signal clearAllFilters()
    signal restoreTrashRequested()
    signal emptyTrashRequested()
    signal settingsRequested()
    signal keyboardShortcutsRequested()
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
            z: 1
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.toolbarRowHeight

            // Sidebar reveal: the sidebar's own collapse button goes away with
            // it, so this is the only pointer route back once it is hidden. It
            // sits on whichever edge the sidebar occupies and animates in from
            // off-window; the row below reserves its width so nothing jumps.
            HoverRect {
                id: sidebarReveal
                readonly property bool shown: !root.sidebarVisible
                readonly property real reservedWidth: shown ? Theme.controlSize + 4 : 0

                width: Theme.controlSize
                height: Theme.controlSize
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: root.sidebarOnRight ? undefined : parent.left
                anchors.right: root.sidebarOnRight ? parent.right : undefined
                anchors.leftMargin: Theme.spacing
                anchors.rightMargin: Theme.spacing
                opacity: shown ? 1.0 : 0.0
                visible: opacity > 0.01
                hoverEnabled: shown
                Accessible.role: Accessible.Button
                tooltip: "Show sidebar"
                shortcut: "toggle_sidebar"
                onClicked: root.sidebarToggleRequested()

                IconPanelLeft {
                    anchors.centerIn: parent
                    size: 18
                    color: sidebarReveal.hovered ? Theme.accent : Theme.text
                    // Slide out of the window edge, and point at the sidebar.
                    x: (sidebarReveal.shown ? 0 : (root.sidebarOnRight ? 8 : -8))
                    transform: Scale {
                        origin.x: 9
                        xScale: root.sidebarOnRight ? -1 : 1
                    }
                    Behavior on x {
                        NumberAnimation { duration: Theme.animDuration; easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve }
                    }
                }

                Behavior on opacity {
                    NumberAnimation { duration: Theme.animDuration; easing.type: Theme.animEasingEnter }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing + (root.sidebarOnRight ? 0 : sidebarReveal.reservedWidth)
                anchors.rightMargin: Theme.spacing + (root.sidebarOnRight ? sidebarReveal.reservedWidth : 0)
                spacing: 4

                Behavior on anchors.leftMargin {
                    NumberAnimation { duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve }
                }
                Behavior on anchors.rightMargin {
                    NumberAnimation { duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve }
                }

                // Left-side window controls
                Repeater {
                    model: root.showWindowControls ? root._parsedLayout.left : []
                    delegate: HoverRect {
                        required property string modelData
                        width: Theme.controlSize; height: Theme.controlSize
                        tooltip: modelData === "close" ? "Close" : modelData === "minimize" ? "Minimize" : "Maximize"
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
                    tooltip: "Back"
                    shortcut: "back"
                    onClicked: root.backRequested()
                    IconChevronLeft { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                // Forward button
                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    hoverEnabled: root.canGoForward
                    opacity: hoverEnabled ? 1.0 : 0.4
                    tooltip: "Forward"
                    shortcut: "forward"
                    onClicked: root.forwardRequested()
                    IconChevronRight { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                // Up button
                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    hoverEnabled: !root.isRecentsView
                    opacity: hoverEnabled ? 1.0 : 0.4
                    tooltip: "Parent folder"
                    shortcut: "parent"
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

                // View switcher: one linked group in the Nautilus manner, not
                // three loose buttons. Same three modes as Ctrl+1/2/3 and the
                // right-click menu, which were the only ways in until now.
                Rectangle {
                    id: viewSwitcher
                    readonly property int segment: Theme.controlSize - 4

                    visible: !root.searchMode
                    Layout.preferredWidth: visible ? viewModeRow.width + 4 : 0
                    Layout.preferredHeight: Theme.controlSize
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                    border.width: 1
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
                    Accessible.role: Accessible.Grouping
                    Accessible.name: "View mode"

                    Row {
                        id: viewModeRow
                        anchors.centerIn: parent
                        spacing: 0

                        Repeater {
                            model: [
                                { mode: "grid",     icon: "Grid",         name: "Grid view" },
                                { mode: "miller",   icon: "Columns3",     name: "Miller column view" },
                                { mode: "detailed", icon: "AlignJustify", name: "Detailed view" }
                            ]

                            delegate: HoverRect {
                                id: viewButton
                                required property var modelData
                                objectName: "viewModeButton_" + modelData.mode
                                readonly property bool current: root.currentViewMode === modelData.mode

                                width: viewSwitcher.segment
                                height: viewSwitcher.segment
                                Accessible.role: Accessible.RadioButton
                                tooltip: modelData.name
                                shortcut: modelData.mode + "_view"
                                Accessible.checked: current

                                // Only the selected segment is painted; the
                                // group's own border draws the outline, so the
                                // segments carry none of their own.
                                color: current
                                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                              hovered ? 0.34 : 0.26)
                                    : (hovered
                                        ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                                        : "transparent")
                                onClicked: root.viewModeRequested(modelData.mode)

                                Loader {
                                    anchors.centerIn: parent
                                    source: "../icons/Icon" + viewButton.modelData.icon + ".qml"
                                    onLoaded: {
                                        item.size = 16
                                        item.color = Qt.binding(() => viewButton.current ? Theme.accent : Theme.text)
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    id: splitSwitcher
                    readonly property int segment: Theme.controlSize - 4

                    visible: !root.searchMode
                    Layout.preferredWidth: visible ? splitModeRow.width + 4 : 0
                    Layout.preferredHeight: Theme.controlSize
                    Layout.alignment: Qt.AlignVCenter
                    radius: Theme.radiusSmall
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                    border.width: 1
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.10)
                    Accessible.role: Accessible.Grouping
                    Accessible.name: "Split view"

                    Row {
                        id: splitModeRow
                        anchors.centerIn: parent
                        spacing: 0

                        Repeater {
                            model: [
                                { orientation: "vertical", name: "Vertical split view" },
                                { orientation: "horizontal", name: "Horizontal split view" }
                            ]

                            delegate: HoverRect {
                                id: splitButton
                                required property var modelData
                                objectName: modelData.orientation + "SplitButton"
                                readonly property bool current: root.splitViewEnabled &&
                                    root.isSplitHorizontal === (modelData.orientation === "horizontal")
                                width: splitSwitcher.segment
                                height: splitSwitcher.segment
                                Accessible.role: Accessible.Button
                                Accessible.name: modelData.name
                                Accessible.checked: current
                                tooltip: modelData.name
                                color: current
                                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                                              hovered ? 0.34 : 0.26)
                                    : (hovered
                                        ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                                        : "transparent")
                                onClicked: root.splitRequested(modelData.orientation)
                                IconSquareSplitHorizontal {
                                    anchors.centerIn: parent
                                    size: 16
                                    rotation: splitButton.modelData.orientation === "horizontal" ? 90 : 0
                                    color: splitButton.current ? Theme.accent : Theme.text
                                }
                            }
                        }
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
                    tooltip: "Search"
                    shortcut: "search"
                    onClicked: root.searchClicked()
                    IconSearch { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    visible: !root.searchMode
                    tooltip: "Keyboard shortcuts"
                    shortcut: "keyboard_shortcuts"
                    onClicked: root.keyboardShortcutsRequested()
                    IconKeyboard { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                HoverRect {
                    width: Theme.controlSize; height: Theme.controlSize
                    visible: !root.searchMode
                    tooltip: "Settings"
                    shortcut: "settings"
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
                        tooltip: modelData === "close" ? "Close" : modelData === "minimize" ? "Minimize" : "Maximize"
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

                Flickable {
                    id: tabStrip
                    objectName: "tabStrip"
                    anchors.fill: parent
                    contentWidth: tabRow.width
                    contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: false   // tabs handle presses; wheel and auto-scroll move the strip
                    clip: true

                    // Scroll position is driven through targetX: contentX has a
                    // Behavior, so reading it back mid-animation returns a stale
                    // value and clamping/accumulating on it cancels the move.
                    property real targetX: 0
                    function scrollTo(x) {
                        targetX = Math.max(0, Math.min(x, contentWidth - width))
                        contentX = targetX
                    }
                    function ensureTabVisible(index) {
                        // While the bar is hidden (count <= 1) the strip has no
                        // width; a scroll computed then would stick as a stale
                        // offset once it opens (first tab cut off, gap on the right).
                        if (index < 0 || tabModel.count === 0 || width <= 0) return
                        var w = tabRow.width / Math.max(tabModel.count, 1)
                        var left = index * w, right = left + w
                        var x = targetX
                        if (left < x) x = left
                        else if (right > x + width) x = right - width
                        scrollTo(x)
                    }
                    onContentWidthChanged: scrollTo(targetX)
                    Connections {
                        target: tabModel
                        function onActiveIndexChanged() { tabStrip.ensureTabVisible(tabModel.activeIndex) }
                        function onCountChanged() { Qt.callLater(function() { tabStrip.ensureTabVisible(tabModel.activeIndex) }) }
                    }
                    onWidthChanged: { scrollTo(targetX); ensureTabVisible(tabModel.activeIndex) }
                    Component.onCompleted: ensureTabVisible(tabModel.activeIndex)
                    Behavior on contentX { NumberAnimation { duration: Theme.animDurationFast; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve } }

                    function scrollByWheel(wheel) {
                        var step = wheel.pixelDelta.x !== 0 ? -wheel.pixelDelta.x
                                 : wheel.pixelDelta.y !== 0 ? -wheel.pixelDelta.y
                                 : -(wheel.angleDelta.y + wheel.angleDelta.x) / 120 * tabRow.minTabWidth
                        scrollTo(targetX + step)
                    }
                    // A WheelHandler only takes events on its own axis, so one per
                    // axis: mouse wheel / vertical swipe, and horizontal swipe.
                    WheelHandler { orientation: Qt.Vertical;   onWheel: (wheel) => tabStrip.scrollByWheel(wheel) }
                    WheelHandler { orientation: Qt.Horizontal; onWheel: (wheel) => tabStrip.scrollByWheel(wheel) }

                    RowLayout {
                        id: tabRow
                        // Tabs never go under minTabWidth: past that the strip
                        // scrolls instead of squeezing them.
                        readonly property int minTabWidth: 120
                        width: Math.max(tabStrip.width, tabModel.count * minTabWidth)
                        height: tabStrip.height
                        spacing: 0

                        // Track how many tabs are closing so others can grow immediately
                        property int closingCount: 0
                        property int effectiveCount: Math.max(tabModel.count - closingCount, 1)
                        property int hoveredIndex: -1
                        property bool reordering: false   // a tab is being dragged: slide displaced tabs

                        Repeater {
                            id: tabRepeater
                            model: tabModel

                            delegate: Rectangle {
                                id: tabDelegate

                                required property int index
                                required property var model
                                objectName: "toolbarTab_" + index

                                Layout.fillHeight: true
                                Layout.preferredWidth: closing ? 0 : tabRow.width / tabRow.effectiveCount
                                property bool closing: false

                                Behavior on Layout.preferredWidth {
                                    NumberAnimation { duration: Theme.animDuration; easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve }
                                }

                                opacity: 0
                                scale: 0.94

                                property int frozenIndex: -1

                                // Reorder animation. The RowLayout snaps x to the new slot; we offset
                                // the tab back to where it was and ease that offset to zero, so it
                                // visibly slides. The dragged tab itself snaps with the pointer.
                                property real slideOffset: 0
                                property real lastX: 0
                                transform: Translate { x: tabDelegate.slideOffset }
                                onXChanged: {
                                    if (tabRow.reordering && !tabMa.dragging) {
                                        slideOffset += lastX - x
                                        slideAnim.restart()
                                    }
                                    lastX = x
                                }
                                NumberAnimation {
                                    id: slideAnim
                                    target: tabDelegate; property: "slideOffset"; to: 0
                                    duration: Theme.animDuration
                                    easing.type: Theme.animEasingTransition; easing.bezierCurve: Theme.animBezierCurve
                                }

                                function startClose() {
                                    if (closing) return
                                    frozenIndex = tabDelegate.index
                                    closing = true
                                    tabRow.closingCount++
                                    exitAnim.start()
                                }

                                Component.onCompleted: { lastX = x; enterAnim.start() }

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
                                            // Live index: closing another tab meanwhile shifts
                                            // ours, a frozen index would remove the wrong tab
                                            // (or none) and leave an invisible zero-width ghost.
                                            tabModel.closeTab(tabDelegate.index)
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
                                    id: tabMa
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                    cursorShape: dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                    preventStealing: true   // keep the window-move DragHandler off a tab drag
                                    property real pressX: 0
                                    property bool dragging: false
                                    onPressed: (mouse) => { pressX = mouse.x; dragging = false }
                                    // Drag sideways to reorder: the model move keeps this
                                    // delegate alive, so the drag simply continues.
                                    onPositionChanged: (mouse) => {
                                        if (!pressed || mouse.buttons !== Qt.LeftButton) return
                                        if (!dragging && Math.abs(mouse.x - pressX) < 6) return
                                        if (!dragging) {
                                            dragging = true
                                            tabRow.reordering = true
                                            tabModel.activeIndex = tabDelegate.index
                                        }
                                        var xInRow = mapToItem(tabRow, mouse.x, 0).x
                                        var slot = tabRow.width / Math.max(tabModel.count, 1)
                                        var target = Math.max(0, Math.min(tabModel.count - 1, Math.floor(xInRow / slot)))
                                        if (target !== tabDelegate.index)
                                            tabModel.moveTab(tabDelegate.index, target)
                                    }
                                    onReleased: tabRow.reordering = false
                                    onClicked: (mouse) => {
                                        if (dragging) { dragging = false; return }
                                        if (mouse.button === Qt.MiddleButton)
                                            tabDelegate.startClose()
                                        else
                                            tabModel.activeIndex = tabDelegate.index
                                    }
                                    onCanceled: { dragging = false; tabRow.reordering = false }
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

                                    ShortcutTip {
                                        text: "Close tab"
                                        action: "close_tab"
                                        shown: closeHover.hovered
                                    }
                                }
                            }
                        }
                    }
                }

                // Fades hint that the strip continues beyond either edge
                Rectangle {
                    anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom
                    width: 24
                    visible: tabStrip.contentX > 0.5
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Theme.mantle }
                        GradientStop { position: 1; color: "transparent" }
                    }
                }
                Rectangle {
                    anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                    width: 24
                    visible: tabStrip.contentX + tabStrip.width < tabStrip.contentWidth - 0.5
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: "transparent" }
                        GradientStop { position: 1; color: Theme.mantle }
                    }
                }
            }
        }
    }
}
