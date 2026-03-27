import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM
import "quill" as Quill

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: "HyprFM"
    color: "transparent"

    // ── Sync fsModel when active tab changes; quit on last tab closed ───────
    Connections {
        target: tabModel
        function onActiveIndexChanged() {
            if (tabModel.activeTab)
                fsModel.setRootPath(tabModel.activeTab.currentPath)
        }
        function onLastTabClosed() {
            Qt.quit()
        }
    }

    Connections {
        target: tabModel.activeTab ?? null
        ignoreUnknownSignals: true
        function onCurrentPathChanged() {
            if (tabModel.activeTab)
                fsModel.setRootPath(tabModel.activeTab.currentPath)
        }
    }

    // Force initial load after QML is fully set up
    Component.onCompleted: {
        if (tabModel.activeTab) {
            fsModel.refresh()
        }

        // Bridge HyprFM theme into Quill theme singleton
        Quill.Theme.background = Qt.binding(() => Theme.base)
        Quill.Theme.backgroundAlt = Qt.binding(() => Theme.mantle)
        Quill.Theme.backgroundDeep = Qt.binding(() => Theme.crust)
        Quill.Theme.surface0 = Qt.binding(() => Theme.surface)
        Quill.Theme.surface1 = Qt.binding(() => Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1))
        Quill.Theme.surface2 = Qt.binding(() => Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15))
        Quill.Theme.textPrimary = Qt.binding(() => Theme.text)
        Quill.Theme.textSecondary = Qt.binding(() => Theme.subtext)
        Quill.Theme.textTertiary = Qt.binding(() => Theme.muted)
        Quill.Theme.primary = Qt.binding(() => Theme.accent)
        Quill.Theme.success = Qt.binding(() => Theme.success)
        Quill.Theme.warning = Qt.binding(() => Theme.warning)
        Quill.Theme.error = Qt.binding(() => Theme.error)
        Quill.Theme.radiusSm = Qt.binding(() => Theme.radiusSmall)
        Quill.Theme.radius = Qt.binding(() => Theme.radiusMedium)
        Quill.Theme.radiusLg = Qt.binding(() => Theme.radiusLarge)
        Quill.Theme.fontSizeSmall = Qt.binding(() => Theme.fontSmall)
        Quill.Theme.fontSize = Qt.binding(() => Theme.fontNormal)
        Quill.Theme.fontSizeLarge = Qt.binding(() => Theme.fontLarge)
    }

    // ── Sidebar visibility (local property; config.sidebarVisible is read-only) ─
    property bool sidebarVisible: config.sidebarVisible

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
            var fp = fsModel.filePath(indices[i])
            if (fp !== "") paths.push(fp)
        }
        return paths
    }

    // ── Helper: list of all file paths in current directory (for preview cycling)
    function getDirectoryFiles() {
        var files = []
        var count = fsModel.rowCount()
        for (var i = 0; i < count; i++) {
            files.push(fsModel.filePath(i))
        }
        return files
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
            height: renameContent.implicitHeight + 40
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: renameBox.yOffset }

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                radius: Theme.radiusMedium
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                border.width: 1
            }

            Column {
                id: renameContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12

                Text {
                    text: "Rename"
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                }

                TextField {
                    id: renameField
                    width: parent.width
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    padding: 8
                    background: Rectangle {
                        color: Theme.surface
                        radius: Theme.radiusSmall
                        border.color: renameField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15)
                        border.width: 1
                    }
                    Keys.onReturnPressed: renameDialog.accept()
                    Keys.onEscapePressed: renameDialog.reject()
                }

                Row {
                    anchors.right: parent.right
                    spacing: 12

                    Text {
                        text: "Cancel"
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSmall
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: renameDialog.reject()
                        }
                    }

                    Rectangle {
                        width: okRenameText.implicitWidth + 24
                        height: 28
                        radius: Theme.radiusSmall
                        color: Theme.accent
                        Text {
                            id: okRenameText
                            text: "Rename"
                            color: Theme.mantle
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: renameDialog.accept()
                        }
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
            height: folderContent.implicitHeight + 40
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: folderBox.yOffset }

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                radius: Theme.radiusMedium
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                border.width: 1
            }

            Column {
                id: folderContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12

                Text {
                    text: "New Folder"
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                }

                TextField {
                    id: newFolderField
                    width: parent.width
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    padding: 8
                    background: Rectangle {
                        color: Theme.surface
                        radius: Theme.radiusSmall
                        border.color: newFolderField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15)
                        border.width: 1
                    }
                    Keys.onReturnPressed: newFolderDialog.accept()
                    Keys.onEscapePressed: newFolderDialog.reject()
                }

                Row {
                    anchors.right: parent.right
                    spacing: 12

                    Text {
                        text: "Cancel"
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSmall
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: newFolderDialog.reject()
                        }
                    }

                    Rectangle {
                        width: okFolderText.implicitWidth + 24
                        height: 28
                        radius: Theme.radiusSmall
                        color: Theme.accent
                        Text {
                            id: okFolderText
                            text: "Create"
                            color: Theme.mantle
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: newFolderDialog.accept()
                        }
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
            height: fileContent.implicitHeight + 40
            anchors.centerIn: parent

            opacity: 0
            scale: 0.88
            transformOrigin: Item.Center

            property real yOffset: 0
            transform: Translate { y: fileBox.yOffset }

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                radius: Theme.radiusMedium
                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                border.width: 1
            }

            Column {
                id: fileContent
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 20
                spacing: 12

                Text {
                    text: "New File"
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                }

                TextField {
                    id: newFileField
                    width: parent.width
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                    padding: 8
                    background: Rectangle {
                        color: Theme.surface
                        radius: Theme.radiusSmall
                        border.color: newFileField.activeFocus ? Theme.accent : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15)
                        border.width: 1
                    }
                    Keys.onReturnPressed: newFileDialog.accept()
                    Keys.onEscapePressed: newFileDialog.reject()
                }

                Row {
                    anchors.right: parent.right
                    spacing: 12

                    Text {
                        text: "Cancel"
                        color: Theme.subtext
                        font.pixelSize: Theme.fontSmall
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: newFileDialog.reject()
                        }
                    }

                    Rectangle {
                        width: okFileText.implicitWidth + 24
                        height: 28
                        radius: Theme.radiusSmall
                        color: Theme.accent
                        Text {
                            id: okFileText
                            text: "Create"
                            color: Theme.mantle
                            font.pixelSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: newFileDialog.accept()
                        }
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
                            color: Theme.subtext; font.pixelSize: Theme.fontSmall; elide: Text.ElideRight; width: parent.width
                        }
                    }
                }

                // ── Tab bar ──
                Item {
                    width: parent.width; height: 36
                    Row {
                        anchors.left: parent.left; anchors.leftMargin: 20; spacing: 0
                        Repeater {
                            model: ["General", "Permissions"]
                            delegate: Item {
                                width: tabText.implicitWidth + 24; height: 36
                                Text {
                                    id: tabText; text: modelData; anchors.centerIn: parent
                                    font.pixelSize: Theme.fontSmall
                                    font.weight: propertiesDialog.currentTab === index ? Font.DemiBold : Font.Normal
                                    color: propertiesDialog.currentTab === index ? Theme.accent : Theme.subtext
                                }
                                Rectangle {
                                    width: parent.width; height: 2; anchors.bottom: parent.bottom
                                    color: Theme.accent; visible: propertiesDialog.currentTab === index
                                    radius: 1
                                }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: propertiesDialog.currentTab = index
                                }
                            }
                        }
                    }
                    Rectangle { width: parent.width; height: 1; anchors.bottom: parent.bottom; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06) }
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
                        Text { text: label; color: Theme.subtext; font.pixelSize: Theme.fontSmall; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: 80 }
                        Text { text: value; color: Theme.text; font.pixelSize: Theme.fontSmall; anchors.left: parent.left; anchors.leftMargin: 88; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter; elide: Text.ElideMiddle }
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
                    Rectangle { width: parent.width - 48; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06) }

                    // Timestamps
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 0

                        PropRow { label: "Created"; value: propertiesDialog.props.created || "" }
                        PropRow { label: "Modified"; value: propertiesDialog.props.modified || "" }
                        PropRow { label: "Accessed"; value: propertiesDialog.props.accessed || "" }
                    }

                    Rectangle { width: parent.width - 48; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06) }

                    // Size section
                    Column {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 24; anchors.rightMargin: 24; spacing: 0

                        PropRow { label: "Size"; value: propertiesDialog.props.sizeText || "" }
                        PropRow { label: "Content"; value: propertiesDialog.props.contentText || ""; show: propertiesDialog.props.isDir || false }
                    }

                    Rectangle { width: parent.width - 48; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06) }

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
                            Text { text: "Usage"; color: Theme.subtext; font.pixelSize: Theme.fontSmall; anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter; width: 80 }
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
                    Rectangle {
                        width: parent.width - 48; height: 1; anchors.horizontalCenter: parent.horizontalCenter
                        color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                        visible: !(propertiesDialog.props.isDir) && propertiesDialog.apps.length > 0
                    }

                    Item {
                        width: parent.width
                        height: openWithInner.height
                        visible: !(propertiesDialog.props.isDir) && propertiesDialog.apps.length > 0

                        property bool expanded: false

                        Column {
                            id: openWithInner
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.leftMargin: 24; anchors.rightMargin: 24
                            spacing: 0

                            Item { width: 1; height: 6 }

                            // Current default app button
                            Rectangle {
                                width: parent.width; height: 32; radius: Theme.radiusSmall
                                color: owBtnMa.containsMouse ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05) : "transparent"

                                Text {
                                    text: "Open with"
                                    color: Theme.subtext; font.pixelSize: Theme.fontSmall
                                    anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                                    width: 80
                                }

                                Text {
                                    id: owCurrentApp
                                    text: {
                                        var apps = propertiesDialog.apps
                                        for (var i = 0; i < apps.length; i++)
                                            if (apps[i].isDefault) return apps[i].name
                                        return apps.length > 0 ? apps[0].name : ""
                                    }
                                    color: Theme.text; font.pixelSize: Theme.fontSmall
                                    anchors.left: parent.left; anchors.leftMargin: 88
                                    anchors.right: owChevron.left; anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    elide: Text.ElideRight
                                }

                                Item {
                                    id: owChevron
                                    width: 12; height: 12
                                    anchors.right: parent.right; anchors.rightMargin: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    rotation: parent.parent.parent.expanded ? 180 : 0
                                    Behavior on rotation { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
                                    IconChevronDown { anchors.centerIn: parent; size: 12; color: Theme.subtext }
                                }

                                MouseArea {
                                    id: owBtnMa; anchors.fill: parent; hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: parent.parent.parent.expanded = !parent.parent.parent.expanded
                                }
                            }

                            // Animated expanding list
                            Item {
                                width: parent.width
                                height: parent.parent.expanded ? owAppList.height + 8 : 0
                                clip: true
                                Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                                Rectangle {
                                    id: owListBg
                                    width: parent.width; height: owAppList.height + 8
                                    radius: Theme.radiusSmall
                                    color: Theme.surface
                                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                                    border.width: 1

                                    Column {
                                        id: owAppList
                                        width: parent.width - 8
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.top: parent.top; anchors.topMargin: 4
                                        spacing: 2

                                        Repeater {
                                            model: propertiesDialog.apps
                                            delegate: Rectangle {
                                                width: parent.width; height: 30; radius: 4
                                                color: owItemMa.containsMouse
                                                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.1)
                                                    : "transparent"

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
                                                    font.pixelSize: Theme.fontSmall
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
                                                        openWithInner.parent.expanded = false
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            Item { width: 1; height: 6 }
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
                        anchors.leftMargin: 24; anchors.rightMargin: 24

                        // Group header
                        Text {
                            text: groupLabel
                            color: Theme.text; font.pixelSize: Theme.fontSmall; font.weight: Font.DemiBold
                            leftPadding: 24
                        }

                        // User name (if any)
                        Text {
                            text: userName; visible: userName !== ""
                            color: Theme.subtext; font.pixelSize: Theme.fontSmall
                            leftPadding: 36
                        }

                        // Access selector row
                        Item {
                            width: parent.width; height: 28
                            Text { text: "Access:"; color: Theme.subtext; font.pixelSize: Theme.fontSmall; anchors.left: parent.left; anchors.leftMargin: 36; anchors.verticalCenter: parent.verticalCenter }

                            // Dropdown-style selector
                            Rectangle {
                                anchors.left: parent.left; anchors.leftMargin: 108
                                anchors.right: parent.right; anchors.rightMargin: 24
                                height: 26; radius: Theme.radiusSmall
                                color: Theme.surface
                                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12); border.width: 1

                                Text {
                                    text: propsBox.accessOptions[accessIdx] || "None"
                                    color: Theme.text; font.pixelSize: Theme.fontSmall
                                    anchors.left: parent.left; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                                }
                                IconChevronDown {
                                    size: 12; color: Theme.subtext
                                    anchors.right: parent.right; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
                                }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        var next = (accessIdx + 1) % 4
                                        accessChanged(next)
                                    }
                                }
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

                    Rectangle { width: parent.width - 48; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06) }

                    PermGroup {
                        groupLabel: "Group"
                        userName: propertiesDialog.props.group || ""
                        accessIdx: propertiesDialog.props.groupAccess || 0
                        onAccessChanged: (idx) => {
                            fsModel.setFilePermissions(propertiesDialog.props.path, propertiesDialog.props.ownerAccess || 0, idx, propertiesDialog.props.otherAccess || 0)
                            propertiesDialog.props = fsModel.fileProperties(propertiesDialog.props.path)
                        }
                    }

                    Rectangle { width: parent.width - 48; height: 1; anchors.horizontalCenter: parent.horizontalCenter; color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06) }

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
                    Rectangle {
                        width: propsCloseText.implicitWidth + 28; height: 30; radius: Theme.radiusSmall
                        color: Theme.accent
                        anchors.right: parent.right; anchors.rightMargin: 20; anchors.verticalCenter: parent.verticalCenter
                        Text { id: propsCloseText; text: "Close"; color: Theme.mantle; font.pixelSize: Theme.fontSmall; font.weight: Font.DemiBold; anchors.centerIn: parent }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: propertiesDialog.close() }
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

    // ── Layout ──────────────────────────────────────────────────────────────
    ColumnLayout {
        id: mainContent
        anchors.fill: parent
        spacing: 0

        // Tab bar
        FileTabBar {
            Layout.fillWidth: true
        }

        // Toolbar with breadcrumb and view mode toggle
        Toolbar {
            id: toolbar
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
                visible: root.sidebarVisible
                currentPath: tabModel.activeTab ? tabModel.activeTab.currentPath : ""
                onBookmarkClicked: (path) => {
                    if (tabModel.activeTab) tabModel.activeTab.navigateTo(path)
                }
            }

            // File view (semi-transparent — Hyprland compositor blurs behind this)
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.65)

            FileViewContainer {
                id: fileViewContainer
                anchors.fill: parent
                fileModel: fsModel
                viewMode: tabModel.activeTab ? tabModel.activeTab.viewMode : "grid"
                currentPath: tabModel.activeTab ? tabModel.activeTab.currentPath : ""

                onFileActivated: (filePath, isDirectory) => {
                    if (isDirectory) {
                        if (tabModel.activeTab) tabModel.activeTab.navigateTo(filePath)
                    } else {
                        fileOps.openFile(filePath)
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
            } // Rectangle wrapper
        }

        // Status bar
        StatusBar {
            id: statusBar
            Layout.fillWidth: true
            itemCount: fsModel.fileCount + fsModel.folderCount
            folderCount: fsModel.folderCount
            selectedCount: root.currentSelectedCount
            selectedSize: root.currentSelectedSize
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
