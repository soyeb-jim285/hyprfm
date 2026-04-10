import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1000
    dialogWidth: Math.min(840, width - 40)
    title: "Settings"
    subtitle: "Appearance and behavior update live while you adjust them."

    property bool currentShowHidden: false
    property bool currentSidebarVisible: true
    property int currentSidebarWidth: 200

    property var themeOptions: []
    property var fontOptions: []
    property var iconThemeOptions: []
    property var availableThemeValues: []
    property var availableFontValues: []
    property var availableIconThemeValues: []
    property bool optionSourcesPrimed: false
    property bool syncingFromConfig: false
    property bool pendingSettingsDirty: false

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
    property int draftAnimDurationFast: config.animDurationFast
    property int draftAnimDuration: config.animDuration
    property int draftAnimDurationSlow: config.animDurationSlow
    property string draftAnimCurveEnter: config.animCurveEnter
    property string draftAnimCurveExit: config.animCurveExit
    property string draftAnimCurveTransition: config.animCurveTransition

    readonly property var curveOptions: ["OutCubic", "InOutCubic", "InCubic", "OutQuad", "InOutQuad", "OutExpo", "InOutExpo", "OutBack", "Linear", "Bezier"]

    property bool draftShowWindowControls: config.showWindowControls
    property string draftWindowButtonLayout: config.windowButtonLayout

    // Helpers to decompose the layout string for the UI
    readonly property var _layoutParts: {
        var layout = draftWindowButtonLayout || ":minimize,maximize,close"
        var parts = layout.split(":")
        var leftStr = parts[0] || ""
        var rightStr = parts.length > 1 ? parts[1] : ""
        var allButtons = []
        if (leftStr) allButtons = allButtons.concat(leftStr.split(",").filter(function(s) { return s.trim() !== "" }))
        if (rightStr) allButtons = allButtons.concat(rightStr.split(",").filter(function(s) { return s.trim() !== "" }))
        return {
            side: leftStr && !rightStr ? "left" : "right",
            hasClose: allButtons.indexOf("close") >= 0,
            hasMinimize: allButtons.indexOf("minimize") >= 0,
            hasMaximize: allButtons.indexOf("maximize") >= 0
        }
    }

    function rebuildButtonLayout(side, hasClose, hasMinimize, hasMaximize) {
        var buttons = []
        if (hasMinimize) buttons.push("minimize")
        if (hasMaximize) buttons.push("maximize")
        if (hasClose) buttons.push("close")
        var str = buttons.join(",")
        draftWindowButtonLayout = side === "left" ? (str + ":") : (":" + str)
        applySettingsNow()
    }

    signal remoteConnectRequested()
    signal keyboardShortcutsRequested()

    readonly property string systemFontLabel: "System Default"

    function primeOptionSources() {
        if (optionSourcesPrimed)
            return

        availableThemeValues = config.availableThemes
        availableFontValues = config.availableFonts
        availableIconThemeValues = config.availableIconThemes
        optionSourcesPrimed = true
    }

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
        for (var i = 0; i < availableFontValues.length; ++i)
            options.push(availableFontValues[i])

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

    function syncFromCurrentState() {
        primeOptionSources()
        syncingFromConfig = true
        try {
            draftTheme = config.theme
            draftDarkMode = isDarkTheme(draftTheme)
            themeOptions = buildOptions(availableThemeValues, draftTheme, "catppuccin-mocha")

            draftFontFamily = config.fontFamily
            fontOptions = buildFontOptions()

            draftIconTheme = config.iconTheme
            iconThemeOptions = buildOptions(availableIconThemeValues, draftIconTheme, "Adwaita")

            draftShowHidden = currentShowHidden
            draftSidebarVisible = currentSidebarVisible
            draftSidebarWidth = currentSidebarWidth
            draftRadiusSmall = config.radiusSmall
            draftRadiusMedium = Math.max(config.radiusMedium, draftRadiusSmall)
            draftRadiusLarge = Math.max(config.radiusLarge, draftRadiusMedium)
            draftTransparencyEnabled = config.transparencyEnabled
            draftTransparencyLevel = config.transparencyLevel
            draftAnimationsEnabled = config.animationsEnabled
            draftAnimDurationFast = config.animDurationFast
            draftAnimDuration = config.animDuration
            draftAnimDurationSlow = config.animDurationSlow
            draftAnimCurveEnter = config.animCurveEnter
            draftAnimCurveExit = config.animCurveExit
            draftAnimCurveTransition = config.animCurveTransition
            draftShowWindowControls = config.showWindowControls
            draftWindowButtonLayout = config.windowButtonLayout
        } finally {
            syncingFromConfig = false
        }
    }

    function openPanel() {
        syncFromCurrentState()
        open()
    }

    function closePanel() {
        flushPendingChanges()
        close()
    }

    function openRemoteConnect() {
        closePanel()
        remoteConnectRequested()
    }

    function openKeyboardShortcuts() {
        closePanel()
        keyboardShortcutsRequested()
    }

    function currentSettings() {
        return {
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
            animationsEnabled: draftAnimationsEnabled,
            animDurationFast: draftAnimDurationFast,
            animDuration: draftAnimDuration,
            animDurationSlow: draftAnimDurationSlow,
            animCurveEnter: draftAnimCurveEnter,
            animCurveExit: draftAnimCurveExit,
            animCurveTransition: draftAnimCurveTransition,
            showWindowControls: draftShowWindowControls,
            windowButtonLayout: draftWindowButtonLayout
        }
    }

    function queueSettingsApply() {
        if (syncingFromConfig)
            return

        pendingSettingsDirty = true
        settingsApplyTimer.restart()
    }

    function applyPendingSettings() {
        if (!pendingSettingsDirty)
            return

        pendingSettingsDirty = false
        settingsApplyTimer.stop()
        config.saveSettings(currentSettings())
    }

    function applySettingsNow() {
        if (syncingFromConfig)
            return

        pendingSettingsDirty = true
        applyPendingSettings()
    }

    function flushPendingChanges() {
        applyPendingSettings()
    }

    onRejected: root.flushPendingChanges()
    onClosed: root.flushPendingChanges()
    Component.onCompleted: root.primeOptionSources()

    Timer {
        id: settingsApplyTimer
        interval: 140
        onTriggered: root.applyPendingSettings()
    }

    Item {
        Layout.fillWidth: true
        implicitHeight: Math.min(560, contentFlick.contentHeight)

        Flickable {
            id: contentFlick
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: contentGrid.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            RowLayout {
                id: contentGrid
                width: contentFlick.width
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
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
                            onToggled: (value) => {
                                root.setDraftTheme(value ? "catppuccin-mocha" : "catppuccin-latte")
                                root.applySettingsNow()
                            }
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
                            onSelected: (_, value) => {
                                root.setDraftTheme(value)
                                root.applySettingsNow()
                            }
                        }

                        Q.Dropdown {
                            Layout.fillWidth: true
                            label: "Font"
                            model: root.fontOptions
                            currentIndex: root.optionIndex(root.fontOptions, root.draftFontFamily === "" ? root.systemFontLabel : root.draftFontFamily, 0)
                            onSelected: (_, value) => {
                                root.draftFontFamily = value === root.systemFontLabel ? "" : value
                                root.applySettingsNow()
                            }
                        }

                        Q.Dropdown {
                            Layout.fillWidth: true
                            label: "Icon pack"
                            model: root.iconThemeOptions
                            currentIndex: root.optionIndex(root.iconThemeOptions, root.draftIconTheme, 0)
                            onSelected: (_, value) => {
                                root.draftIconTheme = value
                                root.applySettingsNow()
                            }
                        }

                        Q.Checkbox {
                            label: "Show hidden files"
                            checked: root.draftShowHidden
                            onToggled: (value) => {
                                root.draftShowHidden = value
                                root.applySettingsNow()
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    radius: Theme.radiusLarge
                    color: Theme.containerColor(Theme.crust, 0.32)
                    border.width: 1
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                    implicitHeight: windowControlsSection.implicitHeight + 32

                    ColumnLayout {
                        id: windowControlsSection
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        Text {
                            text: "Window Controls"
                            color: Theme.text
                            font.pointSize: Theme.fontNormal + 1
                            font.bold: true
                        }

                        Q.Toggle {
                            Layout.fillWidth: true
                            label: "Show window controls"
                            checked: root.draftShowWindowControls
                            onToggled: (value) => {
                                root.draftShowWindowControls = value
                                root.applySettingsNow()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Integrated close, minimize, and maximize buttons in the toolbar."
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        Q.Dropdown {
                            Layout.fillWidth: true
                            label: "Button side"
                            enabled: root.draftShowWindowControls
                            model: ["Right", "Left"]
                            currentIndex: root._layoutParts.side === "left" ? 1 : 0
                            onSelected: (_, value) => {
                                root.rebuildButtonLayout(
                                    value === "Left" ? "left" : "right",
                                    root._layoutParts.hasClose,
                                    root._layoutParts.hasMinimize,
                                    root._layoutParts.hasMaximize
                                )
                            }
                        }

                        Q.Checkbox {
                            label: "Close button"
                            enabled: root.draftShowWindowControls
                            checked: root._layoutParts.hasClose
                            onToggled: (value) => {
                                root.rebuildButtonLayout(root._layoutParts.side, value, root._layoutParts.hasMinimize, root._layoutParts.hasMaximize)
                            }
                        }

                        Q.Checkbox {
                            label: "Minimize button"
                            enabled: root.draftShowWindowControls
                            checked: root._layoutParts.hasMinimize
                            onToggled: (value) => {
                                root.rebuildButtonLayout(root._layoutParts.side, root._layoutParts.hasClose, value, root._layoutParts.hasMaximize)
                            }
                        }

                        Q.Checkbox {
                            label: "Maximize button"
                            enabled: root.draftShowWindowControls
                            checked: root._layoutParts.hasMaximize
                            onToggled: (value) => {
                                root.rebuildButtonLayout(root._layoutParts.side, root._layoutParts.hasClose, root._layoutParts.hasMinimize, value)
                            }
                        }
                    }
                }

                } // end left column

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
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
                            onToggled: (value) => {
                                root.draftSidebarVisible = value
                                root.applySettingsNow()
                            }
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
                            onMoved: (value) => {
                                root.draftSidebarWidth = Math.round(value)
                                root.queueSettingsApply()
                            }
                        }

                        Q.Toggle {
                            Layout.fillWidth: true
                            label: "Transparent containers"
                            checked: root.draftTransparencyEnabled
                            onToggled: (value) => {
                                root.draftTransparencyEnabled = value
                                root.applySettingsNow()
                            }
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
                            onMoved: (value) => {
                                root.draftTransparencyLevel = value / 100
                                root.queueSettingsApply()
                            }
                        }

                        Q.Toggle {
                            Layout.fillWidth: true
                            label: "Animations"
                            checked: root.draftAnimationsEnabled
                            onToggled: (value) => {
                                root.draftAnimationsEnabled = value
                                root.applySettingsNow()
                            }
                        }

                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    radius: Theme.radiusLarge
                    color: Theme.containerColor(Theme.crust, 0.32)
                    border.width: 1
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                    implicitHeight: toolsSection.implicitHeight + 32

                    ColumnLayout {
                        id: toolsSection
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 12

                        Text {
                            text: "Tools"
                            color: Theme.text
                            font.pointSize: Theme.fontNormal + 1
                            font.bold: true
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Open dedicated dialogs for keyboard shortcuts and remote locations."
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        Q.Button {
                            text: "Keyboard Shortcuts"
                            onClicked: root.openKeyboardShortcuts()
                        }

                        Q.Button {
                            text: "Connect to Network Location"
                            variant: "ghost"
                            onClicked: root.openRemoteConnect()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
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
                            text: "Corner radius and transparency update live while you adjust them."
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
                                root.queueSettingsApply()
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
                                root.queueSettingsApply()
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
                            onMoved: (value) => {
                                root.draftRadiusLarge = Math.round(value)
                                root.queueSettingsApply()
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                        }

                        Text {
                            text: "Animation Timing"
                            color: Theme.text
                            font.pointSize: Theme.fontNormal
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Duration in milliseconds for each animation speed tier."
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        Q.Slider {
                            Layout.fillWidth: true
                            label: "Fast"
                            from: 0
                            to: 500
                            stepSize: 10
                            showValue: true
                            enabled: root.draftAnimationsEnabled
                            value: root.draftAnimDurationFast
                            onMoved: (value) => {
                                root.draftAnimDurationFast = Math.round(value)
                                root.queueSettingsApply()
                            }
                        }

                        Q.Slider {
                            Layout.fillWidth: true
                            label: "Normal"
                            from: 0
                            to: 1000
                            stepSize: 10
                            showValue: true
                            enabled: root.draftAnimationsEnabled
                            value: root.draftAnimDuration
                            onMoved: (value) => {
                                root.draftAnimDuration = Math.round(value)
                                root.queueSettingsApply()
                            }
                        }

                        Q.Slider {
                            Layout.fillWidth: true
                            label: "Slow"
                            from: 0
                            to: 1500
                            stepSize: 10
                            showValue: true
                            enabled: root.draftAnimationsEnabled
                            value: root.draftAnimDurationSlow
                            onMoved: (value) => {
                                root.draftAnimDurationSlow = Math.round(value)
                                root.queueSettingsApply()
                            }
                        }

                        Text {
                            text: "Easing Curves"
                            color: Theme.text
                            font.pointSize: Theme.fontNormal
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.fillWidth: true
                            text: "Enter for appearances, Exit for dismissals, Transition for state changes."
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        Q.Dropdown {
                            Layout.fillWidth: true
                            label: "Enter"
                            enabled: root.draftAnimationsEnabled
                            model: root.curveOptions
                            currentIndex: Math.max(0, root.curveOptions.indexOf(root.draftAnimCurveEnter))
                            onSelected: (_, value) => {
                                root.draftAnimCurveEnter = value
                                root.applySettingsNow()
                            }
                        }

                        Q.Dropdown {
                            Layout.fillWidth: true
                            label: "Exit"
                            enabled: root.draftAnimationsEnabled
                            model: root.curveOptions
                            currentIndex: Math.max(0, root.curveOptions.indexOf(root.draftAnimCurveExit))
                            onSelected: (_, value) => {
                                root.draftAnimCurveExit = value
                                root.applySettingsNow()
                            }
                        }

                        Q.Dropdown {
                            Layout.fillWidth: true
                            label: "Transition"
                            enabled: root.draftAnimationsEnabled
                            model: root.curveOptions
                            currentIndex: Math.max(0, root.curveOptions.indexOf(root.draftAnimCurveTransition))
                            onSelected: (_, value) => {
                                root.draftAnimCurveTransition = value
                                root.applySettingsNow()
                            }
                        }
                    }
                }

                } // end right column
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: "Changes apply automatically and are still written back to the config file, so manual edits remain compatible."
            color: Theme.subtext
            font.pointSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Q.Button {
            text: "Done"
            onClicked: root.closePanel()
        }
    }
}
