import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import HyprFM
import Quill as Q

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: "HyprFM"
    color: "transparent"

    property bool isRecentsView: false

    // ── Sync fsModel when active tab changes; quit on last tab closed ───────
    Connections {
        target: tabModel
        function onActiveIndexChanged() {
            if (tabModel.activeTab) {
                root.isRecentsView = false
                fsModel.setRootPath(tabModel.activeTab.currentPath)
            }
        }
        function onLastTabClosed() {
            Qt.quit()
        }
    }

    Connections {
        target: tabModel.activeTab ?? null
        ignoreUnknownSignals: true
        function onCurrentPathChanged() {
            if (tabModel.activeTab) {
                root.isRecentsView = false
                fsModel.setRootPath(tabModel.activeTab.currentPath)
                if (root.searchMode) root.closeSearch()
            }
        }
    }

    // Force initial load after QML is fully set up
    Component.onCompleted: {
        if (tabModel.activeTab) {
            fsModel.refresh()
        }

        // Bridge HyprFM theme into Quill theme singleton
        Q.Theme.background = Qt.binding(() => Theme.base)
        Q.Theme.backgroundAlt = Qt.binding(() => Theme.mantle)
        Q.Theme.backgroundDeep = Qt.binding(() => Theme.crust)
        Q.Theme.surface0 = Qt.binding(() => Theme.surface)
        Q.Theme.surface1 = Qt.binding(() => Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1))
        Q.Theme.surface2 = Qt.binding(() => Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15))
        Q.Theme.textPrimary = Qt.binding(() => Theme.text)
        Q.Theme.textSecondary = Qt.binding(() => Theme.subtext)
        Q.Theme.textTertiary = Qt.binding(() => Theme.muted)
        Q.Theme.primary = Qt.binding(() => Theme.accent)
        Q.Theme.success = Qt.binding(() => Theme.success)
        Q.Theme.warning = Qt.binding(() => Theme.warning)
        Q.Theme.error = Qt.binding(() => Theme.error)
        Q.Theme.radiusSm = Qt.binding(() => Theme.radiusSmall)
        Q.Theme.radius = Qt.binding(() => Theme.radiusMedium)
        Q.Theme.radiusLg = Qt.binding(() => Theme.radiusLarge)
        Q.Theme.fontFamily = Qt.application.font.family
        Q.Theme.fontSizeSmall = Qt.binding(() => Theme.fontSmall)
        Q.Theme.fontSize = Qt.binding(() => Theme.fontNormal)
        Q.Theme.fontSizeLarge = Qt.binding(() => Theme.fontLarge)
    }

    // ── Sidebar visibility (local property; config.sidebarVisible is read-only) ─
    property bool sidebarVisible: config.sidebarVisible

    // ── Search state ──────────────────────────────────────────────────────────
    property bool searchMode: false
    property var debounceTimer: null

    // ── Selection state for StatusBar ────────────────────────────────────────
    property int currentSelectedCount: 0
    property string currentSelectedSize: ""

    function updateSelectionStatus() {
        var vm = tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
        var subView = null
        if (vm === "grid")          subView = fileViewContainer.gridViewItem
        else if (vm === "list")     subView = fileViewContainer.listViewItem
        else if (vm === "detailed") subView = fileViewContainer.detailedViewItem

        if (!subView || !subView.selectedIndices) {
            currentSelectedCount = 0
            currentSelectedSize = ""
            return
        }

        var indices = subView.selectedIndices
        currentSelectedCount = indices.length

        if (indices.length === 0) {
            currentSelectedSize = ""
            return
        }

        // Size display is omitted here as it requires per-file stat
        // (fsModel.data is not Q_INVOKABLE; count alone suffices for the status bar)
        currentSelectedSize = ""
    }

    // ── Helper: collect selected file paths from active view ─────────────────
    function getSelectedPaths() {
        var paths = []
        var view = fileViewContainer
        if (!view) return paths

        // Access the active sub-view's selectedIndices
        var subView = null
        var vm = tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
        if (vm === "grid")          subView = view.gridViewItem
        else if (vm === "list")     subView = view.listViewItem
        else if (vm === "detailed") subView = view.detailedViewItem

        if (!subView || !subView.selectedIndices) return paths

        var indices = subView.selectedIndices
        for (var i = 0; i < indices.length; i++) {
            var fp = root.searchMode ? searchProxy.filePath(indices[i]) : fsModel.filePath(indices[i])
            if (fp !== "") paths.push(fp)
        }
        return paths
    }

    // ── Helper: list of all file paths in current directory (for preview cycling)
    function getDirectoryFiles() {
        var files = []
        var activeModel = root.searchMode ? searchProxy : fsModel
        var count = activeModel.rowCount()
        for (var i = 0; i < count; i++) {
            files.push(activeModel.filePath(i))
        }
        return files
    }

    // ── Search helpers ────────────────────────────────────────────────────────
    function openSearch() {
        searchMode = true
        toolbar.searchMode = true
        searchProxy.switchSourceModel(searchResults)
    }

    function closeSearch() {
        searchMode = false
        toolbar.searchMode = false
        searchProxy.clearSearch()
        searchService.cancelSearch()
        searchProxy.switchSourceModel(fsModel)
    }

    function handleSearchQuery(query) {
        if (debounceTimer) debounceTimer.destroy()
        if (query === "") {
            searchService.cancelSearch()
            searchResults.clear()
            return
        }
        debounceTimer = Qt.createQmlObject(
            'import QtQuick; Timer { interval: 500; running: true; onTriggered: { parent.triggerRecursiveSearch(); destroy() } }',
            root
        )
    }

    function triggerRecursiveSearch() {
        var query = toolbar.searchBar ? toolbar.searchBar.searchQuery : ""
        if (query === "") return
        searchService.startSearch(
            tabModel.activeTab ? tabModel.activeTab.currentPath : fsModel.homePath(),
            query,
            fsModel.showHidden
        )
    }

    function handleSearchEnter() {
        var query = toolbar.searchBar ? toolbar.searchBar.searchQuery : ""
        if (query === "") return
        searchService.startSearch(
            tabModel.activeTab ? tabModel.activeTab.currentPath : fsModel.homePath(),
            query,
            fsModel.showHidden
        )
    }

    function selectFirstSearchResult() {
        if (!searchMode || searchProxy.rowCount() === 0) return
        var vm = tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
        var subView = null
        if (vm === "grid")          subView = fileViewContainer.gridViewItem
        else if (vm === "list")     subView = fileViewContainer.listViewItem
        else if (vm === "detailed") subView = fileViewContainer.detailedViewItem
        if (subView && subView.selectedIndices !== undefined)
            subView.selectedIndices = [0]
    }

    Connections {
        target: searchService
        function onSearchFinished() { root.selectFirstSearchResult() }
    }

    // ── Rename dialog ───────────────────────────────────────────────────────
    property string renameTargetPath: ""

    Item {
        id: renameDialog
        anchors.fill: parent
        visible: false
        z: 1000

        function open() {
            visible = true
            renameBox.opacity = 0
            renameBox.scale = 0.88
            renameBox.yOffset = -8
            renameOpenAnim.start()
            renameField.forceActiveFocus()
        }
        function accept() {
            if (renameTargetPath !== "" && renameField.text.trim() !== "")
                fileOps.rename(renameTargetPath, renameField.text.trim())
            renameCloseAnim.start()
        }
        function reject() { renameCloseAnim.start() }

        ParallelAnimation {
            id: renameOpenAnim
            NumberAnimation {
                target: renameBox; property: "opacity"
                from: 0; to: 1; duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: renameBox; property: "scale"
                from: 0.88; to: 1; duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 0.8
            }
            NumberAnimation {
                target: renameBox; property: "yOffset"
                from: -8; to: 0; duration: 220
                easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            id: renameCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: renameBox; property: "opacity"
                    to: 0; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: renameBox; property: "scale"
                    to: 0.92; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: renameBox; property: "yOffset"
                    to: -4; duration: 120
                    easing.type: Easing.InCubic
                }
            }
            ScriptAction { script: renameDialog.visible = false }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: renameDialog.reject()
        }

        Item {
            id: renameBox
            width: 340
            height: renameCard.implicitHeight
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: renameBox.yOffset }

            Q.Card {
                id: renameCard
                anchors.fill: parent
                title: "Rename"
                padding: 20
                color: Theme.mantle
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)

                Q.TextField {
                    id: renameField
                    Layout.fillWidth: true
                    autoFocus: true
                    variant: "filled"
                    placeholder: "Enter new name"
                    Keys.onReturnPressed: renameDialog.accept()
                    Keys.onEscapePressed: renameDialog.reject()
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Q.Button {
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        onClicked: renameDialog.reject()
                    }

                    Q.Button {
                        text: "Rename"
                        variant: "primary"
                        size: "small"
                        onClicked: renameDialog.accept()
                    }
                }
            }
        }
    }

    // ── New Folder dialog ───────────────────────────────────────────────────
    property string newItemParentPath: ""

    Item {
        id: newFolderDialog
        anchors.fill: parent
        visible: false
        z: 1000

        function open() {
            visible = true
            folderBox.opacity = 0
            folderBox.scale = 0.88
            folderBox.yOffset = -8
            folderOpenAnim.start()
            newFolderField.forceActiveFocus()
        }
        function accept() {
            if (newItemParentPath !== "" && newFolderField.text.trim() !== "")
                fileOps.createFolder(newItemParentPath, newFolderField.text.trim())
            folderCloseAnim.start()
        }
        function reject() { folderCloseAnim.start() }

        ParallelAnimation {
            id: folderOpenAnim
            NumberAnimation {
                target: folderBox; property: "opacity"
                from: 0; to: 1; duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: folderBox; property: "scale"
                from: 0.88; to: 1; duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 0.8
            }
            NumberAnimation {
                target: folderBox; property: "yOffset"
                from: -8; to: 0; duration: 220
                easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            id: folderCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: folderBox; property: "opacity"
                    to: 0; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: folderBox; property: "scale"
                    to: 0.92; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: folderBox; property: "yOffset"
                    to: -4; duration: 120
                    easing.type: Easing.InCubic
                }
            }
            ScriptAction { script: newFolderDialog.visible = false }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: newFolderDialog.reject()
        }

        Item {
            id: folderBox
            width: 340
            height: folderCard.implicitHeight
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: folderBox.yOffset }

            Q.Card {
                id: folderCard
                anchors.fill: parent
                title: "New Folder"
                padding: 20
                color: Theme.mantle
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)

                Q.TextField {
                    id: newFolderField
                    Layout.fillWidth: true
                    autoFocus: true
                    variant: "filled"
                    placeholder: "Folder name"
                    Keys.onReturnPressed: newFolderDialog.accept()
                    Keys.onEscapePressed: newFolderDialog.reject()
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Q.Button {
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        onClicked: newFolderDialog.reject()
                    }

                    Q.Button {
                        text: "Create"
                        variant: "primary"
                        size: "small"
                        onClicked: newFolderDialog.accept()
                    }
                }
            }
        }
    }

    // ── New File dialog ─────────────────────────────────────────────────────
    Item {
        id: newFileDialog
        anchors.fill: parent
        visible: false
        z: 1000

        function open() {
            visible = true
            fileBox.opacity = 0
            fileBox.scale = 0.88
            fileBox.yOffset = -8
            fileOpenAnim.start()
            newFileField.forceActiveFocus()
        }
        function accept() {
            if (newItemParentPath !== "" && newFileField.text.trim() !== "")
                fileOps.createFile(newItemParentPath, newFileField.text.trim())
            fileCloseAnim.start()
        }
        function reject() { fileCloseAnim.start() }

        ParallelAnimation {
            id: fileOpenAnim
            NumberAnimation {
                target: fileBox; property: "opacity"
                from: 0; to: 1; duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: fileBox; property: "scale"
                from: 0.88; to: 1; duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 0.8
            }
            NumberAnimation {
                target: fileBox; property: "yOffset"
                from: -8; to: 0; duration: 220
                easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            id: fileCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: fileBox; property: "opacity"
                    to: 0; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: fileBox; property: "scale"
                    to: 0.92; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: fileBox; property: "yOffset"
                    to: -4; duration: 120
                    easing.type: Easing.InCubic
                }
            }
            ScriptAction { script: newFileDialog.visible = false }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: newFileDialog.reject()
        }

        Item {
            id: fileBox
            width: 340
            height: fileCard.implicitHeight
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: fileBox.yOffset }

            Q.Card {
                id: fileCard
                anchors.fill: parent
                title: "New File"
                color: Theme.mantle
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                padding: 20

                Q.TextField {
                    id: newFileField
                    Layout.fillWidth: true
                    autoFocus: true
                    variant: "filled"
                    placeholder: "File name"
                    Keys.onReturnPressed: newFileDialog.accept()
                    Keys.onEscapePressed: newFileDialog.reject()
                }

                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: 12

                    Q.Button {
                        text: "Cancel"
                        variant: "ghost"
                        size: "small"
                        onClicked: newFileDialog.reject()
                    }

                    Q.Button {
                        text: "Create"
                        variant: "primary"
                        size: "small"
                        onClicked: newFileDialog.accept()
                    }
                }
            }
        }
    }

    // ── Properties dialog ──────────────────────────────────────────────────
    Item {
        id: propertiesDialog
        anchors.fill: parent
        visible: false
        z: 1000

        property var props: ({})
        property var apps: []
        property int currentTab: 0  // 0=General, 1=Permissions, 2=Open With

        function showProperties(path) {
            props = fsModel.fileProperties(path)
            currentTab = 0
            propsTabs.currentIndex = 0
            if (!props.isDir && props.mimeType)
                apps = fsModel.availableApps(props.mimeType)
            else
                apps = []
            visible = true
            propsBox.opacity = 0
            propsBox.scale = 0.88
            propsBox.yOffset = -8
            propsOpenAnim.start()
        }
        function close() { propsCloseAnim.start() }

        ParallelAnimation {
            id: propsOpenAnim
            NumberAnimation {
                target: propsBox; property: "opacity"
                from: 0; to: 1; duration: 180
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: propsBox; property: "scale"
                from: 0.88; to: 1; duration: 250
                easing.type: Easing.OutBack
                easing.overshoot: 0.8
            }
            NumberAnimation {
                target: propsBox; property: "yOffset"
                from: -8; to: 0; duration: 220
                easing.type: Easing.OutCubic
            }
        }
        SequentialAnimation {
            id: propsCloseAnim
            ParallelAnimation {
                NumberAnimation {
                    target: propsBox; property: "opacity"
                    to: 0; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: propsBox; property: "scale"
                    to: 0.92; duration: 120
                    easing.type: Easing.InCubic
                }
                NumberAnimation {
                    target: propsBox; property: "yOffset"
                    to: -4; duration: 120
                    easing.type: Easing.InCubic
                }
            }
            ScriptAction { script: propertiesDialog.visible = false }
        }

        MouseArea {
            anchors.fill: parent
            onClicked: propertiesDialog.close()
        }

        Item {
            id: propsBox
            width: 420
            height: propsOuterCol.height
            anchors.centerIn: parent
            opacity: 0; scale: 0.88; transformOrigin: Item.Center
            property real yOffset: 0
            transform: Translate { y: propsBox.yOffset }

            // Access dropdown options
            property var accessOptions: ["None", "Read only", "Read & Write", "Read, Write & Execute"]

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                radius: Theme.radiusMedium
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                border.width: 1
            }

            Column {
                id: propsOuterCol
                width: parent.width
                spacing: 0

                // ── Hero: icon + name + kind + size ──
                Item {
                    width: parent.width; height: 88
                    Rectangle {
                        id: propsIconBg; width: 52; height: 52; radius: 12
                        color: Theme.surface
                        anchors.left: parent.left; anchors.leftMargin: 24; anchors.verticalCenter: parent.verticalCenter
                        Image {
                            anchors.centerIn: parent; width: 32; height: 32
                            source: propertiesDialog.props.iconName ? ("image://icon/" + propertiesDialog.props.iconName) : ""
                            sourceSize: Qt.size(32, 32); smooth: true
                        }
                    }
                    Column {
                        anchors.left: propsIconBg.right; anchors.leftMargin: 14
                        anchors.right: parent.right; anchors.rightMargin: 24
                        anchors.verticalCenter: parent.verticalCenter; spacing: 2
                        Text {
                            text: propertiesDialog.props.name || ""; color: Theme.text
                            font.pixelSize: 15; font.weight: Font.DemiBold
                            elide: Text.ElideMiddle; width: parent.width
                        }
                        Text {
                            text: { var p = propertiesDialog.props; return !p.mimeDescription ? "" : p.isDir ? "Folder" : p.mimeDescription }
                            color: Theme.subtext; font.pointSize: Theme.fontSmall; elide: Text.ElideRight; width: parent.width
                        }
                    }
                }

                // ── Tab bar ──
                Q.Tabs {
                    id: propsTabs
                    width: parent.width
                    model: ["General", "Permissions"]
                    currentIndex: propertiesDialog.currentTab
                    onTabChanged: (index) => propertiesDialog.currentTab = index
                }

                // ── Tab content slider ──
                Item {
                    id: tabSlider
                    width: parent.width
                    height: propertiesDialog.currentTab === 0 ? generalTab.height : permissionsTab.height
                    clip: true
                    Behavior on height { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

                    Row {
                        id: tabSliderRow
                        x: -propertiesDialog.currentTab * tabSlider.width
                        Behavior on x { NumberAnimation { duration: 300; easing.type: Easing.OutCubic } }

                // ══════════════════════════════════════════════
                // TAB 0: General
                // ══════════════════════════════════════════════
                Column {
                    id: generalTab
                    width: tabSlider.width; spacing: 0

                    // helper component for a property row
                    component PropRow: Item {
                        property string label
                        property string value
                        property bool show: true
                        width: parent.width; height: show ? 28 : 0; visible: show
                        Text { text: label; color: Theme.subtext; font.pointSize: Theme.fontSmall; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: 80 }
                        Text { text: value; color: Theme.text; font.pointSize: Theme.fontSmall; anchors.left: parent.left; anchors.leftMargin: 88; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideMiddle }
                    }

                    Item { width: 1; height: 8 }

                    // Info rows
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 0

                        PropRow { label: "Kind"; value: { var p = propertiesDialog.props; return p.isDir ? "Folder" : (p.mimeDescription || "") } }
                        PropRow { label: "Location"; value: propertiesDialog.props.parentDir || "" }
                        PropRow { label: "Link target"; value: propertiesDialog.props.symlinkTarget || ""; show: propertiesDialog.props.isSymlink || false }
                    }

                    // Separator
                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    // Timestamps
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 0

                        PropRow { label: "Created"; value: propertiesDialog.props.created || "" }
                        PropRow { label: "Modified"; value: propertiesDialog.props.modified || "" }
                        PropRow { label: "Accessed"; value: propertiesDialog.props.accessed || "" }
                    }

                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    // Size section
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 0

                        PropRow { label: "Size"; value: propertiesDialog.props.sizeText || "" }
                        PropRow { label: "Content"; value: propertiesDialog.props.contentText || ""; show: propertiesDialog.props.isDir || false }
                    }

                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    // Disk usage
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 4
                        visible: propertiesDialog.props.diskTotal !== undefined

                        Item { width: 1; height: 4 }

                        PropRow { label: "Capacity"; value: propertiesDialog.props.diskTotal || "" }

                        // Usage bar
                        Item {
                            width: parent.width; height: 28
                            Text { text: "Usage"; color: Theme.subtext; font.pointSize: Theme.fontSmall; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: 80 }
                            Column {
                                anchors.left: parent.left; anchors.leftMargin: 88
                                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; spacing: 4

                                // Bar
                                Rectangle {
                                    width: parent.width; height: 6; radius: 3
                                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                                    Rectangle {
                                        width: parent.width * (propertiesDialog.props.diskUsedPercent || 0)
                                        height: parent.height; radius: 3
                                        color: (propertiesDialog.props.diskUsedPercent || 0) > 0.9 ? "#e74c3c" : Theme.accent
                                    }
                                }

                                // Label
                                Text {
                                    text: (propertiesDialog.props.diskUsed || "") + " used (" + (propertiesDialog.props.diskUsedPctText || "") + ")  |  " +
                                          (propertiesDialog.props.diskFree || "") + " free (" + (propertiesDialog.props.diskFreePctText || "") + ")"
                                    color: Theme.subtext; font.pixelSize: 10
                                }
                            }
                        }

                        Item { width: 1; height: 4 }
                    }

                    // Open With (files only)
                    Q.Collapsible {
                        visible: !(propertiesDialog.props.isDir) && propertiesDialog.apps.length > 0
                        title: {
                            var apps = propertiesDialog.apps
                            for (var i = 0; i < apps.length; i++)
                                if (apps[i].isDefault) return "Open with: " + apps[i].name
                            return apps.length > 0 ? "Open with: " + apps[0].name : "Open with"
                        }
                        width: parent.width

                        Repeater {
                            model: propertiesDialog.apps
                            delegate: Rectangle {
                                width: parent ? parent.width : 0; height: 30; radius: 4
                                color: owItemMa.containsMouse
                                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.1)
                                    : "transparent"
                                Layout.fillWidth: true

                                Image {
                                    id: owAppIcon
                                    source: modelData.iconName ? ("image://icon/" + modelData.iconName) : ""
                                    sourceSize: Qt.size(18, 18)
                                    width: 18; height: 18
                                    anchors.left: parent.left; anchors.leftMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: modelData.iconName && status === Image.Ready
                                }

                                Text {
                                    text: modelData.name
                                    color: modelData.isDefault ? Theme.accent : Theme.text
                                    font.pointSize: Theme.fontSmall
                                    font.weight: modelData.isDefault ? Font.DemiBold : Font.Normal
                                    anchors.left: owAppIcon.visible ? owAppIcon.right : parent.left
                                    anchors.leftMargin: owAppIcon.visible ? 8 : 10
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.right: owItemBadge.left; anchors.rightMargin: 4
                                    elide: Text.ElideRight
                                }

                                IconCheck {
                                    id: owItemBadge
                                    visible: modelData.isDefault
                                    size: 14; color: Theme.accent
                                    anchors.right: parent.right; anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                MouseArea {
                                    id: owItemMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (!modelData.isDefault) {
                                            fsModel.setDefaultApp(propertiesDialog.props.mimeType, modelData.desktopFile)
                                            propertiesDialog.apps = fsModel.availableApps(propertiesDialog.props.mimeType)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ══════════════════════════════════════════════
                // TAB 1: Permissions
                // ══════════════════════════════════════════════
                Column {
                    id: permissionsTab
                    width: tabSlider.width; spacing: 0

                    Item { width: 1; height: 12 }

                    // Helper component for permission group
                    component PermGroup: Column {
                        property string groupLabel
                        property string userName
                        property int accessIdx: 0
                        signal accessChanged(int newIdx)
                        width: parent.width; spacing: 4

                        // Group header
                        Text {
                            text: groupLabel
                            color: Theme.text; font.pointSize: Theme.fontSmall; font.weight: Font.DemiBold
                            leftPadding: 24
                        }

                        // User name (if any)
                        Text {
                            text: userName; visible: userName !== ""
                            color: Theme.subtext; font.pointSize: Theme.fontSmall
                            leftPadding: 36
                        }

                        // Access selector row
                        Item {
                            width: parent.width; height: 34
                            Text {
                                text: "Access"
                                color: Theme.subtext; font.pointSize: Theme.fontSmall
                                anchors.left: parent.left; anchors.leftMargin: 36
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Q.Dropdown {
                                model: propsBox.accessOptions
                                currentIndex: accessIdx
                                label: ""
                                anchors.left: parent.left; anchors.leftMargin: 100
                                anchors.right: parent.right; anchors.rightMargin: 24
                                onSelected: (index, value) => accessChanged(index)
                            }
                        }

                        Item { width: 1; height: 4 }
                    }

                    PermGroup {
                        groupLabel: "Owner"
                        userName: propertiesDialog.props.owner || ""
                        accessIdx: propertiesDialog.props.ownerAccess || 0
                        onAccessChanged: (idx) => {
                            fsModel.setFilePermissions(propertiesDialog.props.path, idx, propertiesDialog.props.groupAccess || 0, propertiesDialog.props.otherAccess || 0)
                            propertiesDialog.props = fsModel.fileProperties(propertiesDialog.props.path)
                        }
                    }

                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    PermGroup {
                        groupLabel: "Group"
                        userName: propertiesDialog.props.group || ""
                        accessIdx: propertiesDialog.props.groupAccess || 0
                        onAccessChanged: (idx) => {
                            fsModel.setFilePermissions(propertiesDialog.props.path, propertiesDialog.props.ownerAccess || 0, idx, propertiesDialog.props.otherAccess || 0)
                            propertiesDialog.props = fsModel.fileProperties(propertiesDialog.props.path)
                        }
                    }

                    Q.Separator { width: parent.width - 48; anchors.horizontalCenter: parent.horizontalCenter }

                    PermGroup {
                        groupLabel: "Others"
                        userName: ""
                        accessIdx: propertiesDialog.props.otherAccess || 0
                        onAccessChanged: (idx) => {
                            fsModel.setFilePermissions(propertiesDialog.props.path, propertiesDialog.props.ownerAccess || 0, propertiesDialog.props.groupAccess || 0, idx)
                            propertiesDialog.props = fsModel.fileProperties(propertiesDialog.props.path)
                        }
                    }

                    Item { width: 1; height: 8 }
                }

                    } // Row (tabSliderRow)
                } // Item (tabSlider)

                // ── Close button ──
                Item {
                    width: parent.width; height: 48
                    Q.Button {
                        text: "Close"
                        variant: "primary"
                        size: "small"
                        anchors.right: parent.right; anchors.rightMargin: 20
                        anchors.verticalCenter: parent.verticalCenter
                        onClicked: propertiesDialog.close()
                    }
                }
            }
        }
    }

    // ── Context Menu ────────────────────────────────────────────────────────
    ContextMenu {
        id: contextMenu
        blurSource: mainContent

        fileModel: fsModel
        currentViewMode: tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
        currentSortBy: tabModel.activeTab ? tabModel.activeTab.sortBy : "name"
        currentSortAscending: tabModel.activeTab ? tabModel.activeTab.sortAscending : true

        onOpenRequested: (path) => fileOps.openFile(path)
        onOpenWithRequested: (path, desktopFile) => fileOps.openFileWith(path, desktopFile)

        onCutRequested: (paths) => clipboard.cut(paths)

        onCopyRequested: (paths) => clipboard.copy(paths)

        onPasteRequested: (destPath) => {
            var wasCut = clipboard.isCut
            var items = clipboard.take()
            if (!items || items.length === 0) return
            if (wasCut)
                fileOps.moveFiles(items, destPath)
            else
                fileOps.copyFiles(items, destPath)
        }

        onCopyPathRequested: (path) => fileOps.copyPathToClipboard(path)

        onRenameRequested: (path) => {
            root.renameTargetPath = path
            var name = path.substring(path.lastIndexOf("/") + 1)
            renameField.text = name
            renameDialog.open()
        }

        onTrashRequested: (paths) => fileOps.trashFiles(paths)

        onDeleteRequested: (paths) => fileOps.deleteFiles(paths)

        onOpenInTerminalRequested: (path) => {
            fileOps.openInTerminal(path)
        }

        onNewFolderRequested: (parentPath) => {
            root.newItemParentPath = parentPath
            newFolderField.text = ""
            newFolderDialog.open()
        }

        onNewFileRequested: (parentPath) => {
            root.newItemParentPath = parentPath
            newFileField.text = ""
            newFileDialog.open()
        }

        onSelectAllRequested: fileViewContainer.selectAll()

        onPropertiesRequested: (path) => {
            propertiesDialog.showProperties(path)
        }

        onViewModeRequested: (mode) => {
            if (tabModel.activeTab) tabModel.activeTab.viewMode = mode
        }

        onSortRequested: (column, ascending) => {
            if (tabModel.activeTab) {
                tabModel.activeTab.sortBy = column
                tabModel.activeTab.sortAscending = ascending
            }
            if (fsModel) fsModel.sortByColumn(column, ascending)
        }
    }

    // ── Keyboard Shortcuts ──────────────────────────────────────────────────

    // Tab management
    Shortcut {
        sequence: config.shortcut("new_tab")
        onActivated: tabModel.addTab()
    }

    Shortcut {
        sequence: config.shortcut("close_tab")
        onActivated: {
            if (tabModel.count > 1) tabModel.closeTab(tabModel.activeIndex)
        }
    }

    Shortcut {
        sequence: config.shortcut("reopen_tab")
        onActivated: tabModel.reopenClosedTab()
    }

    // Navigation
    Shortcut {
        sequence: config.shortcut("back")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.goBack() }
    }

    Shortcut {
        sequence: "Backspace"
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.goBack() }
    }

    Shortcut {
        sequence: config.shortcut("forward")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.goForward() }
    }

    Shortcut {
        sequence: config.shortcut("parent")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.goUp() }
    }

    // Toggle hidden files
    Shortcut {
        sequence: config.shortcut("toggle_hidden")
        onActivated: fsModel.showHidden = !fsModel.showHidden
    }

    // Toggle path bar focus (Ctrl+L-like)
    Shortcut {
        sequence: config.shortcut("path_bar")
        onActivated: toolbar.startEditing()
    }

    // Toggle sidebar
    Shortcut {
        sequence: config.shortcut("toggle_sidebar")
        onActivated: root.sidebarVisible = !root.sidebarVisible
    }

    // View mode switching
    Shortcut {
        sequence: config.shortcut("grid_view")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.viewMode = "grid" }
    }

    Shortcut {
        sequence: config.shortcut("list_view")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.viewMode = "list" }
    }

    Shortcut {
        sequence: config.shortcut("detailed_view")
        onActivated: { if (tabModel.activeTab) tabModel.activeTab.viewMode = "detailed" }
    }

    // File operations
    Shortcut {
        sequence: config.shortcut("copy")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length > 0) clipboard.copy(paths)
        }
    }

    Shortcut {
        sequence: config.shortcut("cut")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length > 0) clipboard.cut(paths)
        }
    }

    Shortcut {
        sequence: config.shortcut("paste")
        onActivated: {
            if (!clipboard.hasContent) return
            var dest = tabModel.activeTab ? tabModel.activeTab.currentPath : ""
            if (dest === "") return
            var wasCut = clipboard.isCut
            var items = clipboard.take()
            if (!items || items.length === 0) return
            if (wasCut)
                fileOps.moveFiles(items, dest)
            else
                fileOps.copyFiles(items, dest)
        }
    }

    Shortcut {
        sequence: config.shortcut("trash")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length > 0) fileOps.trashFiles(paths)
        }
    }

    Shortcut {
        sequence: config.shortcut("select_all")
        onActivated: fileViewContainer.selectAll()
    }

    Shortcut {
        sequence: config.shortcut("rename")
        onActivated: {
            var paths = getSelectedPaths()
            if (paths.length === 1) {
                root.renameTargetPath = paths[0]
                var name = paths[0].substring(paths[0].lastIndexOf("/") + 1)
                renameField.text = name
                renameDialog.open()
            }
        }
    }

    Shortcut {
        sequence: config.shortcut("new_folder")
        onActivated: {
            var dest = tabModel.activeTab ? tabModel.activeTab.currentPath : ""
            if (dest !== "") {
                root.newItemParentPath = dest
                newFolderField.text = ""
                newFolderDialog.open()
            }
        }
    }

    Shortcut {
        sequence: config.shortcut("new_file")
        onActivated: {
            var dest = tabModel.activeTab ? tabModel.activeTab.currentPath : ""
            if (dest !== "") {
                root.newItemParentPath = dest
                newFileField.text = ""
                newFileDialog.open()
            }
        }
    }

    // Quick preview (spacebar)
    Shortcut {
        sequence: "Space"
        onActivated: {
            if (quickPreview.active) {
                quickPreview.active = false
                return
            }
            var paths = getSelectedPaths()
            if (paths.length === 0) return
            quickPreview.filePath = paths[0]
            quickPreview.directoryFiles = getDirectoryFiles()
            quickPreview.active = true
            quickPreview.forceActiveFocus()
        }
    }

    // Search
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            if (root.searchMode) root.closeSearch()
            else root.openSearch()
        }
    }

    // ── Layout ──────────────────────────────────────────────────────────────
    RowLayout {
        id: mainContent
        anchors.fill: parent
        spacing: 0

        // Sidebar (full height)
        Sidebar {
            width: config.sidebarWidth
            Layout.fillHeight: true
            visible: root.sidebarVisible
            currentPath: tabModel.activeTab ? tabModel.activeTab.currentPath : ""
            isRecentsView: root.isRecentsView
            onBookmarkClicked: (path) => {
                root.isRecentsView = false
                if (tabModel.activeTab) tabModel.activeTab.navigateTo(path)
            }
            onRecentsClicked: {
                root.isRecentsView = true
            }
            onCollapseClicked: root.sidebarVisible = !root.sidebarVisible
        }

        // Right panel: toolbar + content
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

            // Toolbar with integrated tabs
            Toolbar {
                id: toolbar
                Layout.fillWidth: true
                activeTab: tabModel.activeTab
                isRecentsView: root.isRecentsView
                onHomeClicked: {
                    if (tabModel.activeTab)
                        tabModel.activeTab.navigateTo(fsModel.homePath())
                }
                onSearchClicked: root.openSearch()
                onSearchClosed: root.closeSearch()
                onSearchQueryChanged: (query) => root.handleSearchQuery(query)
                onSearchEnterPressed: root.handleSearchEnter()
                onSearchNavigateDown: {
                    var vm = tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
                    var subView = null
                    if (vm === "grid")          subView = fileViewContainer.gridViewItem
                    else if (vm === "list")     subView = fileViewContainer.listViewItem
                    else if (vm === "detailed") subView = fileViewContainer.detailedViewItem
                    if (subView) subView.forceActiveFocus()
                }
                onSearchFilterToggled: {
                    if (toolbar.filterPanel) {
                        toolbar.filterPanel.visible = !toolbar.filterPanel.visible
                    }
                }
                onTypeFilterChanged: (filter) => searchProxy.fileTypeFilter = filter
                onDateFilterChanged: (filter) => searchProxy.dateFilter = filter
                onSizeFilterChanged: (filter) => searchProxy.sizeFilter = filter
                onClearAllFilters: {
                    searchProxy.fileTypeFilter = ""
                    searchProxy.dateFilter = ""
                    searchProxy.sizeFilter = ""
                }
            }

            // File view (semi-transparent — Hyprland compositor blurs behind this)
            Rectangle {
                id: contentArea
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.65)

                // Curved mantle fills for inverse rounded corners
                Shape {
                    z: 1; width: Theme.radiusMedium; height: Theme.radiusMedium
                    anchors.top: parent.top; anchors.left: parent.left
                    ShapePath {
                        fillColor: Theme.mantle; strokeColor: "transparent"
                        startX: 0; startY: 0
                        PathLine { x: Theme.radiusMedium; y: 0 }
                        PathArc {
                            x: 0; y: Theme.radiusMedium
                            radiusX: Theme.radiusMedium; radiusY: Theme.radiusMedium
                            direction: PathArc.Counterclockwise
                        }
                        PathLine { x: 0; y: 0 }
                    }
                }
                Shape {
                    z: 1; width: Theme.radiusMedium; height: Theme.radiusMedium
                    anchors.top: parent.top; anchors.right: parent.right
                    ShapePath {
                        fillColor: Theme.mantle; strokeColor: "transparent"
                        startX: Theme.radiusMedium; startY: 0
                        PathLine { x: 0; y: 0 }
                        PathArc {
                            x: Theme.radiusMedium; y: Theme.radiusMedium
                            radiusX: Theme.radiusMedium; radiusY: Theme.radiusMedium
                            direction: PathArc.Clockwise
                        }
                        PathLine { x: Theme.radiusMedium; y: 0 }
                    }
                }

                FileViewContainer {
                    id: fileViewContainer
                    anchors.fill: parent
                    fileModel: root.isRecentsView ? recentFiles : (root.searchMode ? searchProxy : fsModel)
                    viewMode: tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
                    currentPath: tabModel.activeTab ? tabModel.activeTab.currentPath : ""

                    onFileActivated: (filePath, isDirectory) => {
                        if (isDirectory) {
                            if (tabModel.activeTab) tabModel.activeTab.navigateTo(filePath)
                        } else {
                            fileOps.openFile(filePath)
                            recentFiles.addRecent(filePath)
                        }
                    }

                    onSelectionChanged: root.updateSelectionStatus()

                    onContextMenuRequested: (filePath, isDirectory, position) => {
                        var currentDir = tabModel.activeTab ? tabModel.activeTab.currentPath : ""
                        contextMenu.targetPath = filePath !== "" ? filePath : currentDir
                        contextMenu.targetIsDir = filePath !== "" ? isDirectory : true
                        contextMenu.isEmptySpace = (filePath === "")
                        var sel = getSelectedPaths()
                        contextMenu.selectedPaths = (sel.length > 1) ? sel : (filePath !== "" ? [filePath] : [])
                        contextMenu.popup(position.x, position.y)
                    }
                }
            }

            StatusBar {
                Layout.fillWidth: true
                itemCount: root.isRecentsView ? recentFiles.count
                    : (root.searchMode ? searchProxy.rowCount() : fsModel.fileCount + fsModel.folderCount)
                folderCount: root.isRecentsView ? 0 : (root.searchMode ? 0 : fsModel.folderCount)
                searchStatus: root.searchMode && searchService.isSearching
                    ? "Searching... " + searchService.resultCount + " results"
                    : (root.searchMode && searchProxy.searchActive
                        ? searchProxy.rowCount() + " results"
                        : "")
                selectedCount: root.currentSelectedCount
                selectedSize: root.currentSelectedSize
            }
            }
        }
    }

    // ── Mouse back/forward button support ────────────────────────────────────
    MouseArea {
        anchors.fill: parent
        z: -100
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        propagateComposedEvents: true
        onClicked: (mouse) => {
            if (mouse.button === Qt.BackButton && tabModel.activeTab)
                tabModel.activeTab.goBack()
            else if (mouse.button === Qt.ForwardButton && tabModel.activeTab)
                tabModel.activeTab.goForward()
        }
    }

    // ── Quick Preview overlay (on top of everything) ─────────────────────────
    QuickPreview {
        id: quickPreview
        anchors.fill: parent
        z: 100
        onClosed: quickPreview.active = false
    }

    // ── Toast notifications ──────────────────────────────────────────────────
    Toast {
        id: toast
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
    }

    Connections {
        target: fileOps
        function onOperationFinished(success, error) {
            if (success)
                toast.show("Operation completed successfully", "success")
            else
                toast.show(error || "Operation failed", "error")
        }
    }
}
