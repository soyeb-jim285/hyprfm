import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Item {
    id: root
    anchors.fill: parent
    z: 1000
    visible: panelOpen || closeTimer.running
    focus: panelOpen

    Accessible.role: Accessible.Dialog
    Accessible.name: "Settings"

    property bool panelOpen: false
    property bool currentShowHidden: false
    property bool currentSidebarVisible: true
    property int currentSidebarWidth: 200

    property var themeOptions: []
    property var fontOptions: []
    property var iconThemeOptions: []
    property var shortcutEntries: []
    property var draftShortcuts: ({})

    property string draftTheme: config.theme
    property string draftFontFamily: config.fontFamily
    property string draftIconTheme: config.iconTheme
    property bool draftDarkMode: true
    property bool draftShowHidden: currentShowHidden
    property bool draftSidebarVisible: currentSidebarVisible
    property int draftSidebarWidth: currentSidebarWidth
    property int draftRadiusSmall: config.radiusSmall
    property int draftRadiusMedium: config.radiusMedium
    property int draftRadiusLarge: config.radiusLarge
    property bool draftTransparencyEnabled: config.transparencyEnabled
    property real draftTransparencyLevel: config.transparencyLevel
    property bool draftAnimationsEnabled: config.animationsEnabled

    signal closed()
    signal remoteConnectRequested()

    readonly property int panelWidth: Math.min(520, Math.max(360, width - 24))
    readonly property string systemFontLabel: "System Default"

    function buildOptions(values, currentValue, fallbackValue) {
        var options = []
        for (var i = 0; i < values.length; ++i)
            options.push(values[i])

        var preferredValue = currentValue !== "" ? currentValue : fallbackValue
        if (preferredValue && options.indexOf(preferredValue) === -1)
            options.unshift(preferredValue)

        if (options.length === 0 && fallbackValue)
            options.push(fallbackValue)

        return options
    }

    function buildFontOptions() {
        var options = [systemFontLabel]
        for (var i = 0; i < config.availableFonts.length; ++i)
            options.push(config.availableFonts[i])

        if (draftFontFamily !== "" && options.indexOf(draftFontFamily) === -1)
            options.push(draftFontFamily)

        return options
    }

    function optionIndex(options, value, fallbackIndex) {
        var index = options.indexOf(value)
        return index >= 0 ? index : fallbackIndex
    }

    function isDarkTheme(themeName) {
        return themeName !== "catppuccin-latte"
    }

    function setDraftTheme(themeName) {
        draftTheme = themeName
        draftDarkMode = isDarkTheme(themeName)
    }

    function syncShortcutDrafts() {
        shortcutEntries = config.shortcutDefinitions

        var nextShortcuts = ({})
        for (var i = 0; i < shortcutEntries.length; ++i) {
            var entry = shortcutEntries[i]
            nextShortcuts[entry.action] = entry.sequence
        }
        draftShortcuts = nextShortcuts
    }

    function shortcutValue(action) {
        var value = draftShortcuts[action]
        return value === undefined ? "" : value
    }

    function setShortcutValue(action, value) {
        var nextShortcuts = ({})
        for (var key in draftShortcuts)
            nextShortcuts[key] = draftShortcuts[key]
        nextShortcuts[action] = value
        draftShortcuts = nextShortcuts
    }

    function syncFromCurrentState() {
        draftTheme = config.theme
        draftDarkMode = isDarkTheme(draftTheme)
        themeOptions = buildOptions(config.availableThemes, draftTheme, "catppuccin-mocha")

        draftFontFamily = config.fontFamily
        fontOptions = buildFontOptions()

        draftIconTheme = config.iconTheme
        iconThemeOptions = buildOptions(config.availableIconThemes, draftIconTheme, "Adwaita")

        draftShowHidden = currentShowHidden
        draftSidebarVisible = currentSidebarVisible
        draftSidebarWidth = currentSidebarWidth
        draftRadiusSmall = config.radiusSmall
        draftRadiusMedium = Math.max(config.radiusMedium, draftRadiusSmall)
        draftRadiusLarge = Math.max(config.radiusLarge, draftRadiusMedium)
        draftTransparencyEnabled = config.transparencyEnabled
        draftTransparencyLevel = config.transparencyLevel
        draftAnimationsEnabled = config.animationsEnabled
        syncShortcutDrafts()
    }

    function openPanel() {
        closeTimer.stop()
        syncFromCurrentState()
        panelOpen = true
        Qt.callLater(function() {
            panelCard.forceActiveFocus()
        })
    }

    function closePanel() {
        if (!panelOpen)
            return

        panelOpen = false
        closeTimer.restart()
    }

    function openRemoteConnect() {
        closePanel()
        remoteConnectRequested()
    }

    function applySettings() {
        config.saveSettings({
            theme: draftTheme,
            fontFamily: draftFontFamily,
            iconTheme: draftIconTheme,
            showHidden: draftShowHidden,
            sidebarVisible: draftSidebarVisible,
            sidebarWidth: draftSidebarWidth,
            radiusSmall: draftRadiusSmall,
            radiusMedium: draftRadiusMedium,
            radiusLarge: draftRadiusLarge,
            transparencyEnabled: draftTransparencyEnabled,
            transparencyLevel: draftTransparencyLevel,
            animationsEnabled: draftAnimationsEnabled
        })
        config.saveShortcuts(draftShortcuts)
        closePanel()
    }

    Keys.onEscapePressed: (event) => {
        if (!panelOpen)
            return

        event.accepted = true
        closePanel()
    }

    Timer {
        id: closeTimer
        interval: 220
        onTriggered: root.closed()
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(Theme.base.r, Theme.base.g, Theme.base.b, 0.58)
        opacity: root.panelOpen ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.animDurationFast; easing.type: Easing.OutCubic }
        }

        MouseArea {
            anchors.fill: parent
            enabled: root.panelOpen
            onClicked: root.closePanel()
        }
    }

    Rectangle {
        id: panelCard
        x: root.width - width + (root.panelOpen ? 0 : width + 18)
        y: 0
        width: root.panelWidth
        height: root.height
        radius: Theme.radiusLarge
        color: Theme.containerColor(Theme.mantle, 0.96)
        border.width: 1
        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
        clip: true

        Behavior on x {
            NumberAnimation { duration: Theme.animDurationSlow; easing.type: Easing.OutCubic }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 38
                    Layout.preferredHeight: 38
                    radius: 12
                    color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)

                    IconSettings {
                        anchors.centerIn: parent
                        size: 18
                        color: Theme.accent
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        text: "Settings"
                        color: Theme.text
                        font.pointSize: Theme.fontLarge + 2
                        font.bold: true
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Appearance, behavior, shortcuts, and remote access all live here now."
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                    }
                }

                HoverRect {
                    Layout.alignment: Qt.AlignTop
                    width: 32
                    height: 32
                    onClicked: root.closePanel()

                    IconX {
                        anchors.centerIn: parent
                        size: 16
                        color: Theme.text
                    }
                }
            }

            Flickable {
                id: contentFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: width
                contentHeight: contentColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: contentColumn
                    width: contentFlick.width
                    spacing: 12

                    Rectangle {
                        width: parent.width
                        radius: Theme.radiusLarge
                        color: Theme.containerColor(Theme.crust, 0.32)
                        border.width: 1
                        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                        implicitHeight: generalSection.implicitHeight + 32

                        ColumnLayout {
                            id: generalSection
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            Text {
                                text: "General"
                                color: Theme.text
                                font.pointSize: Theme.fontNormal + 1
                                font.bold: true
                            }

                            Q.Toggle {
                                Layout.fillWidth: true
                                label: "Dark mode"
                                checked: root.draftDarkMode
                                onToggled: (value) => root.setDraftTheme(value ? "catppuccin-mocha" : "catppuccin-latte")
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "The toggle switches between Catppuccin Mocha and Catppuccin Latte."
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }

                            Q.Dropdown {
                                Layout.fillWidth: true
                                label: "Theme"
                                model: root.themeOptions
                                currentIndex: root.optionIndex(root.themeOptions, root.draftTheme, 0)
                                onSelected: (_, value) => root.setDraftTheme(value)
                            }

                            Q.Dropdown {
                                Layout.fillWidth: true
                                label: "Font"
                                model: root.fontOptions
                                currentIndex: root.optionIndex(root.fontOptions, root.draftFontFamily === "" ? root.systemFontLabel : root.draftFontFamily, 0)
                                onSelected: (_, value) => root.draftFontFamily = value === root.systemFontLabel ? "" : value
                            }

                            Q.Dropdown {
                                Layout.fillWidth: true
                                label: "Icon pack"
                                model: root.iconThemeOptions
                                currentIndex: root.optionIndex(root.iconThemeOptions, root.draftIconTheme, 0)
                                onSelected: (_, value) => root.draftIconTheme = value
                            }

                            Q.Checkbox {
                                label: "Show hidden files"
                                checked: root.draftShowHidden
                                onToggled: (value) => root.draftShowHidden = value
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        radius: Theme.radiusLarge
                        color: Theme.containerColor(Theme.crust, 0.32)
                        border.width: 1
                        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                        implicitHeight: windowSection.implicitHeight + 32

                        ColumnLayout {
                            id: windowSection
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            Text {
                                text: "Window"
                                color: Theme.text
                                font.pointSize: Theme.fontNormal + 1
                                font.bold: true
                            }

                            Q.Checkbox {
                                label: "Show sidebar"
                                checked: root.draftSidebarVisible
                                onToggled: (value) => root.draftSidebarVisible = value
                            }

                            Q.Slider {
                                Layout.fillWidth: true
                                label: "Sidebar width"
                                from: 160
                                to: 480
                                stepSize: 10
                                showValue: true
                                enabled: root.draftSidebarVisible
                                value: root.draftSidebarWidth
                                onMoved: (value) => root.draftSidebarWidth = Math.round(value)
                            }

                            Q.Toggle {
                                Layout.fillWidth: true
                                label: "Transparent containers"
                                checked: root.draftTransparencyEnabled
                                onToggled: (value) => root.draftTransparencyEnabled = value
                            }

                            Q.Slider {
                                Layout.fillWidth: true
                                label: "Transparency"
                                from: 0
                                to: 100
                                stepSize: 1
                                showValue: true
                                enabled: root.draftTransparencyEnabled
                                value: root.draftTransparencyLevel * 100
                                onMoved: (value) => root.draftTransparencyLevel = value / 100
                            }

                            Q.Toggle {
                                Layout.fillWidth: true
                                label: "Animations"
                                checked: root.draftAnimationsEnabled
                                onToggled: (value) => root.draftAnimationsEnabled = value
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        radius: Theme.radiusLarge
                        color: Theme.containerColor(Theme.crust, 0.32)
                        border.width: 1
                        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                        implicitHeight: appearanceSection.implicitHeight + 32

                        ColumnLayout {
                            id: appearanceSection
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            Text {
                                text: "Appearance"
                                color: Theme.text
                                font.pointSize: Theme.fontNormal + 1
                                font.bold: true
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Corner radius updates the shell and most controls after saving."
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }

                            Q.Slider {
                                Layout.fillWidth: true
                                label: "Small radius"
                                from: 0
                                to: 24
                                stepSize: 1
                                showValue: true
                                value: root.draftRadiusSmall
                                onMoved: (value) => {
                                    root.draftRadiusSmall = Math.round(value)
                                    if (root.draftRadiusMedium < root.draftRadiusSmall)
                                        root.draftRadiusMedium = root.draftRadiusSmall
                                    if (root.draftRadiusLarge < root.draftRadiusMedium)
                                        root.draftRadiusLarge = root.draftRadiusMedium
                                }
                            }

                            Q.Slider {
                                Layout.fillWidth: true
                                label: "Medium radius"
                                from: root.draftRadiusSmall
                                to: 28
                                stepSize: 1
                                showValue: true
                                value: root.draftRadiusMedium
                                onMoved: (value) => {
                                    root.draftRadiusMedium = Math.round(value)
                                    if (root.draftRadiusLarge < root.draftRadiusMedium)
                                        root.draftRadiusLarge = root.draftRadiusMedium
                                }
                            }

                            Q.Slider {
                                Layout.fillWidth: true
                                label: "Large radius"
                                from: root.draftRadiusMedium
                                to: 32
                                stepSize: 1
                                showValue: true
                                value: root.draftRadiusLarge
                                onMoved: (value) => root.draftRadiusLarge = Math.round(value)
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        radius: Theme.radiusLarge
                        color: Theme.containerColor(Theme.crust, 0.32)
                        border.width: 1
                        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                        implicitHeight: networkSection.implicitHeight + 32

                        ColumnLayout {
                            id: networkSection
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            Text {
                                text: "Remote Access"
                                color: Theme.text
                                font.pointSize: Theme.fontNormal + 1
                                font.bold: true
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Connect to SFTP, SMB, FTP, or a direct URI from here instead of the toolbar."
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }

                            Q.Button {
                                text: "Connect to Network Location"
                                onClicked: root.openRemoteConnect()
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        radius: Theme.radiusLarge
                        color: Theme.containerColor(Theme.crust, 0.32)
                        border.width: 1
                        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                        implicitHeight: shortcutsSection.implicitHeight + 32

                        ColumnLayout {
                            id: shortcutsSection
                            anchors.fill: parent
                            anchors.margins: 16
                            spacing: 12

                            Text {
                                text: "Keyboard Shortcuts"
                                color: Theme.text
                                font.pointSize: Theme.fontNormal + 1
                                font.bold: true
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Edit the shortcut text directly. Clear a field to fall back to the built-in default."
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }

                            Repeater {
                                model: root.shortcutEntries

                                delegate: Rectangle {
                                    required property var modelData

                                    Layout.fillWidth: true
                                    implicitHeight: shortcutColumn.implicitHeight + 16
                                    radius: Theme.radiusMedium
                                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                                    border.width: 1
                                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)

                                    ColumnLayout {
                                        id: shortcutColumn
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 6

                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 10

                                            Text {
                                                Layout.fillWidth: true
                                                text: modelData.label
                                                color: Theme.text
                                                font.pointSize: Theme.fontNormal
                                                font.bold: true
                                            }

                                            Q.TextField {
                                                Layout.preferredWidth: 170
                                                Layout.maximumWidth: 200
                                                variant: "filled"
                                                placeholder: modelData.defaultSequence
                                                text: modelData.sequence
                                                onTextEdited: root.setShortcutValue(modelData.action, text)
                                            }
                                        }

                                        Text {
                                            Layout.fillWidth: true
                                            text: "Default: " + modelData.defaultSequence
                                            color: Theme.subtext
                                            font.pointSize: Theme.fontSmall
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: "The config file stays the source of truth, so manual edits still work."
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }

                Q.Button {
                    text: "Cancel"
                    variant: "ghost"
                    onClicked: root.closePanel()
                }

                Q.Button {
                    text: "Save"
                    onClicked: root.applySettings()
                }
            }
        }
    }
}
