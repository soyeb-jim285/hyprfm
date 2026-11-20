import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM

FocusScope {
    id: root
    property var deviceModel: computerModel
    property var deviceActions: devices
    property string viewMode: "grid"
    readonly property var selectedIndices: [] // Drives are not filesystem-operation targets.
    property var selectedDevices: []
    property int cursorIndex: -1
    property int anchorIndex: -1
    property var savedSelection: []
    property string savedCursor: ""
    property string savedAnchor: ""
    signal fileActivated(string filePath, bool isDirectory)
    signal openInNewTabRequested(string path)
    signal openInSplitViewRequested(string path)
    signal featureHintRequested(string message)
    signal interactionStarted()
    signal selectionChanged()
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Computer")
    function selectAll() {
        var selection = []
        for (var i = 0; i < deviceModel.count; ++i) selection.push(i)
        selectedDevices = selection
        selectionChanged()
    }
    function focusPath(path, reveal) { return false }
    function deviceKey(drive) {
        // The fallback root and the discovered root can have different device
        // names. All other volumes keep their identity when mounted/unmounted.
        return drive.mountPoint === "/" ? "/" : drive.devicePath || drive.mountPoint || ""
    }
    function saveSelection() {
        savedSelection = selectedDevices.map(index => deviceKey(deviceModel.get(index)))
        savedCursor = deviceKey(deviceModel.get(cursorIndex))
        savedAnchor = deviceKey(deviceModel.get(anchorIndex))
    }
    function restoreSelection() {
        var selection = []
        var cursor = -1
        var anchor = -1
        for (var index = 0; index < deviceModel.count; ++index) {
            const key = deviceKey(deviceModel.get(index))
            if (key && savedSelection.indexOf(key) >= 0) selection.push(index)
            if (key && key === savedCursor) cursor = index
            if (key && key === savedAnchor) anchor = index
        }
        selectedDevices = selection
        cursorIndex = cursor
        anchorIndex = anchor
        selectionChanged()
    }
    function choose(index, control, shift) {
        if (index < 0 || index >= deviceModel.count) return
        if (shift && anchorIndex >= 0) {
            var range = []
            for (var i = Math.min(index, anchorIndex); i <= Math.max(index, anchorIndex); ++i) range.push(i)
            selectedDevices = range
        } else if (control) {
            var values = selectedDevices.slice()
            var position = values.indexOf(index)
            if (position >= 0) values.splice(position, 1)
            else values.push(index)
            selectedDevices = values
            anchorIndex = index
        } else {
            selectedDevices = [index]
            anchorIndex = index
        }
        cursorIndex = index
        grid.positionViewAtIndex(index, GridView.Contain)
        interactionStarted()
        forceActiveFocus()
        selectionChanged()
    }
    function openDevice(index) {
        const drive = deviceModel.get(index)
        if (drive.mounted && drive.mountPoint) fileActivated(drive.mountPoint, true)
        else if (drive.backend === "mtp-missing") featureHintRequested(runtimeFeatures.installHint("mtpDevice"))
        else if (drive.sourceIndex >= 0) deviceActions.mount(drive.sourceIndex)
    }
    function selectedMountPath() {
        if (selectedDevices.length !== 1) return ""
        const drive = deviceModel.get(selectedDevices[0])
        return drive.mounted ? drive.mountPoint || "" : ""
    }
    function formatBytes(value, unitFor) {
        const units = ["B", "KiB", "MiB", "GiB", "TiB"]
        var power = Math.max(0, Math.min(4, Math.floor(Math.log(Math.max(1, unitFor || value)) / Math.log(1024))))
        return (value / Math.pow(1024, power)).toFixed(power ? 1 : 0) + " " + units[power]
    }
    function refresh() { deviceActions.refreshAsync(); deviceModel.refresh() }
    Keys.onPressed: event => {
        // Only plain and Shift (range) presses move through the drives;
        // Alt+Left/Right (history), Ctrl+Return (new tab) and the like stay
        // with the window's shortcuts.
        if (event.modifiers & (Qt.AltModifier | Qt.ControlModifier | Qt.MetaModifier)) {
            event.accepted = false
            return
        }
        var step = 0
        if (event.key === Qt.Key_Left) step = -1
        else if (event.key === Qt.Key_Right) step = 1
        else if (event.key === Qt.Key_Up) step = -Math.max(1, Math.floor(grid.width / grid.cellWidth))
        else if (event.key === Qt.Key_Down) step = Math.max(1, Math.floor(grid.width / grid.cellWidth))
        else if (event.key === Qt.Key_Home) { choose(0, false, !!(event.modifiers & Qt.ShiftModifier)); event.accepted = true; return }
        else if (event.key === Qt.Key_End) { choose(deviceModel.count - 1, false, !!(event.modifiers & Qt.ShiftModifier)); event.accepted = true; return }
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { openDevice(cursorIndex); event.accepted = true; return }
        else if (event.key === Qt.Key_Escape) { selectedDevices = []; cursorIndex = -1; anchorIndex = -1; selectionChanged(); event.accepted = true; return }
        if (step) { choose(Math.max(0, Math.min(deviceModel.count - 1, cursorIndex < 0 ? 0 : cursorIndex + step)), false, !!(event.modifiers & Qt.ShiftModifier)); event.accepted = true }
    }
    Connections {
        target: root.deviceModel
        function onModelAboutToBeReset() {
            root.saveSelection()
            driveMenu.close()
            driveMenu.driveIndex = -1
        }
        function onModelReset() { root.restoreSelection() }
    }
    Component.onCompleted: deviceModel.refresh()
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            IconMonitor { size: 24; color: Theme.accent }
            Text { text: qsTr("Computer"); color: Theme.text; font.pointSize: Theme.fontLarge; Layout.fillWidth: true }
            HoverRect {
                width: Theme.controlSize; height: Theme.controlSize
                tooltip: qsTr("Refresh devices")
                onClicked: root.refresh()
                IconRefreshCw { anchors.centerIn: parent; size: 18; color: Theme.text }
            }
        }
        Text { text: qsTr("Devices and drives"); color: Theme.subtext; font.pointSize: Theme.fontSmall }
        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.deviceModel
            readonly property bool rows: root.viewMode === "detailed" || root.viewMode === "miller"
            readonly property bool small: rows || root.viewMode === "compact"
            cellWidth: rows ? width : Math.max(180, width / Math.max(1, Math.floor(width / 230)))
            cellHeight: small ? 112 : 172
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Rectangle {
                id: card
                objectName: "computerDrive_" + index
                required property int index
                required property string deviceName
                required property string devicePath
                required property string mountPoint
                required property bool mounted
                required property bool capacityKnown
                required property real totalSize
                required property real freeSpace
                required property int usagePercent
                required property string deviceIcon
                required property int sourceIndex
                readonly property bool selected: root.selectedDevices.indexOf(index) >= 0
                width: grid.rows ? Math.min(grid.cellWidth - 8, 480 * Theme.uiScale) : grid.cellWidth - 8
                height: grid.cellHeight - 8
                radius: Theme.radiusMedium
                color: selected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : Theme.surface
                border.width: selected ? 2 : 1
                border.color: selected ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                Accessible.role: Accessible.ListItem
                Accessible.name: deviceName
                Accessible.selected: selected
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 5
                    RowLayout {
                        Layout.fillWidth: true
                        Image { source: Theme.fileIconSource(card.deviceIcon, "", grid.small ? 36 : 48, false); sourceSize: Qt.size(96,96); Layout.preferredWidth: grid.small ? 36 : 48; Layout.preferredHeight: grid.small ? 36 : 48 }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text { text: card.deviceName; color: Theme.text; font.pointSize: Theme.fontNormal; elide: Text.ElideRight; Layout.fillWidth: true }
                            Text { text: card.mountPoint || card.devicePath; color: Theme.subtext; font.pointSize: Theme.fontSmall; elide: Text.ElideMiddle; Layout.fillWidth: true }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 5
                        radius: 2
                        visible: card.capacityKnown
                        color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
                        Rectangle {
                            width: parent.width * (card.freeSpace < card.totalSize ? Math.max(0.02, card.usagePercent / 100) : 0)
                            height: parent.height
                            radius: 2
                            color: Theme.usageColor(card.usagePercent)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: card.capacityKnown ? qsTr("%1 free of %2").arg(root.formatBytes(card.freeSpace, card.totalSize)).arg(root.formatBytes(card.totalSize))
                            : card.mounted ? qsTr("Capacity unavailable") : qsTr("Not mounted")
                        color: Theme.subtext; font.pointSize: Theme.fontSmall; elide: Text.ElideRight
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: mouse => {
                        root.choose(card.index, !!(mouse.modifiers & Qt.ControlModifier), !!(mouse.modifiers & Qt.ShiftModifier))
                        if (mouse.button === Qt.RightButton) { driveMenu.driveIndex = card.index; driveMenu.popup() }
                    }
                    onDoubleClicked: root.openDevice(card.index)
                }
            }
        }
    }
    // Any press on the page makes its pane the active one and takes the
    // keyboard, as a press on a file view's empty space does, then goes on to
    // the card, button or list under it. Only a drive card did this before,
    // so a click elsewhere on the page left the other split pane active: F4
    // toggled that pane's terminal and a sidebar place opened over there.
    MouseArea {
        objectName: "computerPagePress"
        anchors.fill: parent
        z: 100
        acceptedButtons: Qt.AllButtons
        onPressed: mouse => {
            root.interactionStarted()
            root.forceActiveFocus()
            mouse.accepted = false
        }
    }
    Menu {
        id: driveMenu
        palette.window: Theme.surface
        palette.base: Theme.surface
        palette.text: Theme.text
        palette.windowText: Theme.text
        palette.buttonText: Theme.text
        palette.highlight: Theme.accent
        palette.highlightedText: Theme.base
        property int driveIndex: -1
        readonly property var drive: root.deviceModel.get(driveIndex)
        MenuItem { text: driveMenu.drive.mounted ? qsTr("Open") : qsTr("Mount and open"); onTriggered: root.openDevice(driveMenu.driveIndex) }
        MenuItem {
            text: qsTr("Open in New Tab")
            enabled: !!driveMenu.drive.mounted
            onTriggered: root.openInNewTabRequested(driveMenu.drive.mountPoint)
        }
        MenuItem {
            text: qsTr("Open in Split View")
            enabled: !!driveMenu.drive.mounted
            onTriggered: root.openInSplitViewRequested(driveMenu.drive.mountPoint)
        }
        MenuItem {
            text: qsTr("Unmount")
            visible: !!driveMenu.drive.mounted && driveMenu.drive.mountPoint !== "/" && driveMenu.drive.sourceIndex >= 0
            onTriggered: root.deviceActions.unmount(driveMenu.drive.sourceIndex)
        }
    }
}
