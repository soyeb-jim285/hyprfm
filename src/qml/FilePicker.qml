import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import HyprFM
import "components" as Components
import "views" as Views
import Quill as Q

Window {
    id: root
    objectName: "hyprfmFilePicker"
    width: 980
    height: 640
    minimumWidth: 620
    minimumHeight: 400
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    title: (picker.options.title || (saving ? qsTr("Save file") : qsTr("Open file"))) + " — HyprFM"

    readonly property bool saving: picker.options.mode === "save"
    property string viewMode: config.defaultView === "grid" ? "grid" : "detailed"
    property bool sidebarVisible: true
    property var selectedPaths: []
    property var history: []    // seeded once below; a binding would rewrite it on navigation
    property int historyIndex: 0
    readonly property bool hasOverlay: overwriteDialog.visible || newFolder.visible
    readonly property string acceptLabel: (picker.options.accept_label
        || (saving ? qsTr("Save") : picker.options.directory ? qsTr("Select folder") : qsTr("Open"))).replace(/_/g, "")

    function navigate(path, remember = true) {
        if (!picker.navigate(path)) return
        if (remember) {
            history = history.slice(0, historyIndex + 1).concat([fsModel.rootPath])
            historyIndex = history.length - 1
        }
        selectedPaths = []
    }
    function goBack() {
        if (historyIndex > 0) navigate(history[--historyIndex], false)
    }
    function goForward() {
        if (historyIndex + 1 < history.length) navigate(history[++historyIndex], false)
    }
    function submit(overwrite = false) {
        picker.submit(fsModel.rootPath, filename.text, selectedPaths, overwrite)
    }
    function selectionChanged() {
        const view = files.activeView()
        const indices = view ? view.selectedIndices : []
        selectedPaths = indices.map(index => picker.filePath(index))
        if (saving && indices.length === 1 && !picker.isDir(indices[0]))
            filename.text = picker.fileName(indices[0])
    }
    function focusFiles() {
        const view = files.activeView()
        if (view) view.forceActiveFocus()
    }

    Component.onCompleted: {
        history = [fsModel.rootPath]
    }
    Components.QuillThemeBinding {}
    onVisibleChanged: {
        if (!visible) return
        Qt.callLater(() => {
            if (root.saving) { filename.inputItem.forceActiveFocus(); filename.inputItem.selectAll() }
            else root.focusFiles()
        })
    }

    Shortcut { sequence: "Escape"; enabled: !root.hasOverlay; onActivated: root.close() }
    Shortcut { sequence: "Ctrl+L"; enabled: !root.hasOverlay; onActivated: toolbar.startEditing() }
    Shortcut { sequence: "Alt+Up"; enabled: !root.hasOverlay; onActivated: root.navigate(fsModel.rootPath + "/..") }
    Shortcut { sequence: "Alt+Left"; enabled: !root.hasOverlay; onActivated: root.goBack() }
    Shortcut { sequence: "Alt+Right"; enabled: !root.hasOverlay; onActivated: root.goForward() }
    Shortcut { sequence: "Ctrl+H"; enabled: !root.hasOverlay; onActivated: fsModel.showHidden = !fsModel.showHidden }
    Shortcut { sequence: "Ctrl+Return"; enabled: !root.hasOverlay; onActivated: root.submit() }
    Shortcut { sequence: "Ctrl+Shift+N"; enabled: !root.hasOverlay; onActivated: newFolder.open() }

    Rectangle {
        anchors.fill: parent
        Accessible.role: Accessible.Dialog
        Accessible.name: root.title
        radius: Theme.radiusLarge
        color: Theme.mantle
        border.width: 1
        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacing
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                LayoutMirroring.enabled: config.sidebarPosition === "right"
                LayoutMirroring.childrenInherit: false

                Components.Sidebar {
                    objectName: "pickerSidebar"
                    Layout.preferredWidth: 176
                    Layout.fillHeight: true
                    visible: root.sidebarVisible && root.width >= 700
                    filePickerMode: true
                    currentPath: fsModel.rootPath
                    onBookmarkClicked: path => root.navigate(path)
                    onSavedSplitClicked: split => root.navigate(split.primaryPath)
                    onCollapseClicked: root.sidebarVisible = false
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0
                    Components.Toolbar {
                        id: toolbar
                        objectName: "pickerToolbar"
                        Layout.fillWidth: true
                        z: 2
                        filePickerMode: true
                        window: root
                        activeTab: null
                        navigationPath: fsModel.rootPath
                        canGoBack: root.historyIndex > 0
                        canGoForward: root.historyIndex + 1 < root.history.length
                        sidebarVisible: root.sidebarVisible && root.width >= 700
                        sidebarOnRight: config.sidebarPosition === "right"
                        currentViewMode: root.viewMode
                        showWindowControls: true
                        windowButtonLayout: ":close"
                        onSidebarToggleRequested: root.sidebarVisible = true
                        onNavigateRequested: path => root.navigate(path)
                        onBackRequested: root.goBack()
                        onForwardRequested: root.goForward()
                        onUpRequested: root.navigate(fsModel.rootPath + "/..")
                        onViewModeRequested: mode => { root.viewMode = mode; root.selectedPaths = []; root.focusFiles() }
                        onNewFolderRequested: newFolder.open()
                        onCloseRequested: root.close()
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        radius: Theme.radiusMedium
                        color: Theme.containerColor(Theme.base, 0.65)
                        border.width: 1
                        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                        Views.FileViewContainer {
                            id: files
                            objectName: "pickerFiles"
                            anchors.fill: parent
                            anchors.margins: Theme.spacing
                            fileModel: picker
                            currentPath: fsModel.rootPath
                            viewMode: root.viewMode
                            onFileActivated: (path, isDirectory) => {
                                if (isDirectory) root.navigate(path)
                                else if (root.saving) {
                                    filename.text = path.substring(path.lastIndexOf("/") + 1)
                                    root.submit()
                                } else {
                                    if (!picker.options.multiple || !root.selectedPaths.length)
                                        root.selectedPaths = [path]
                                    root.submit()
                                }
                            }
                            onSelectionChanged: root.selectionChanged()
                            onSortRequested: (column, ascending) => fsModel.sortByColumn(column, ascending)
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Theme.spacing
                Layout.topMargin: Theme.spacing * 2
                spacing: Theme.spacing
                enabled: !root.hasOverlay

                RowLayout {
                    visible: root.saving
                    Layout.fillWidth: true
                    spacing: Theme.spacing * 2
                    Text { text: qsTr("Name"); color: Theme.subtext; font.pointSize: Theme.fontNormal; Layout.preferredWidth: 60 }
                    Q.TextField {
                        id: filename
                        objectName: "pickerFilename"
                        Layout.fillWidth: true
                        placeholder: qsTr("File name")
                        text: picker.options.current_name || ""
                        inputItem.activeFocusOnTab: true
                        inputItem.Accessible.name: qsTr("File name")
                        inputItem.KeyNavigation.tab: cancelButton
                        onSubmitted: root.submit()
                    }
                }
                RowLayout {
                    visible: (picker.options.filters || []).length > 0
                    Layout.fillWidth: true
                    spacing: Theme.spacing * 2
                    Text { text: qsTr("Type"); color: Theme.subtext; font.pointSize: Theme.fontNormal; Layout.preferredWidth: 60 }
                    Q.Dropdown {
                        objectName: "pickerFilter"
                        Layout.fillWidth: true
                        model: (picker.options.filters || []).map(filter => filter.name)
                        currentIndex: picker.filterIndex
                        Accessible.name: qsTr("File type")
                        onSelected: index => { picker.filterIndex = index; root.selectedPaths = [] }
                    }
                }
                Repeater {
                    model: picker.options.choices || []
                    RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Q.Checkbox {
                            visible: modelData.items.length === 0
                            label: modelData.label
                            checked: modelData.selected === "true"
                            onToggled: value => picker.setChoice(modelData.id, value ? "true" : "false")
                        }
                        Text {
                            visible: modelData.items.length > 0
                            text: modelData.label
                            color: Theme.subtext
                            font.pointSize: Theme.fontNormal
                            Layout.preferredWidth: 140
                            Layout.maximumWidth: 140
                            elide: Text.ElideRight
                        }
                        Q.Dropdown {
                            visible: modelData.items.length > 0
                            Layout.fillWidth: true
                            model: modelData.items.map(item => item.label)
                            currentIndex: Math.max(0, modelData.items.findIndex(item => item.id === modelData.selected))
                            Accessible.name: modelData.label
                            onSelected: index => picker.setChoice(modelData.id, modelData.items[index].id)
                        }
                    }
                }
                Text {
                    visible: picker.error.length > 0
                    text: picker.error
                    color: Theme.error
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.Wrap
                    Layout.fillWidth: true
                    Accessible.role: Accessible.AlertMessage
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing
                    Text {
                        text: root.selectedPaths.length ? qsTr("%n selected", "", root.selectedPaths.length) : ""
                        Layout.fillWidth: true
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }
                    Q.Button {
                        id: cancelButton
                        objectName: "pickerCancel"
                        text: qsTr("Cancel")
                        variant: "ghost"
                        KeyNavigation.tab: acceptButton
                        onClicked: root.close()
                    }
                    Q.Button {
                        id: acceptButton
                        objectName: "pickerAccept"
                        text: root.acceptLabel
                        KeyNavigation.backtab: cancelButton
                        KeyNavigation.tab: filename.visible ? filename.inputItem : cancelButton
                        onClicked: root.submit()
                    }
                }
            }
        }
    }
    Connections { target: picker; function onOverwriteRequested() { overwriteDialog.open() } }
    Components.ConfirmDialog {
        id: overwriteDialog
        objectName: "pickerOverwrite"
        title: qsTr("Replace file?")
        message: qsTr("A file with this name already exists. Do you want to replace it?")
        confirmText: qsTr("Replace")
        onAccepted: root.submit(true)
    }
    Q.Dialog {
        id: newFolder
        objectName: "pickerNewFolder"
        anchors.fill: parent
        z: 9998
        title: qsTr("New folder")
        dialogWidth: 340
        initialFocusItem: folderName.inputItem
        property string errorText: ""
        property bool submitting: false
        function createFolder() {
            errorText = ""
            submitting = true
            fileOps.createFolder(fsModel.rootPath, folderName.text)
            submitting = false
            if (!errorText) close()
        }
        onOpened: { folderName.text = ""; errorText = "" }
        Q.TextField {
            id: folderName
            objectName: "pickerFolderName"
            Layout.fillWidth: true
            placeholder: qsTr("Folder name")
            inputItem.activeFocusOnTab: true
            inputItem.Accessible.name: qsTr("Folder name")
            onSubmitted: newFolder.createFolder()
        }
        Text { text: newFolder.errorText; visible: text.length > 0; color: Theme.error; wrapMode: Text.Wrap; Layout.fillWidth: true }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            Q.Button { text: qsTr("Cancel"); variant: "ghost"; onClicked: newFolder.reject() }
            Q.Button { text: qsTr("Create"); onClicked: newFolder.createFolder() }
        }
    }
    Connections {
        target: fileOps
        function onOperationFinished(success, error) {
            if (newFolder.submitting && !success) newFolder.errorText = error
        }
    }
}
