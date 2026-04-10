import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1000
    dialogWidth: Math.min(920, width - 32)
    title: ""
    subtitle: ""
    dialogPadding: 0
    dialogColor: "transparent"
    dialogBorderColor: "transparent"
    dialogRadius: root.draftRadiusLarge + 6

    readonly property color sectionBorderColor: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
    readonly property string defaultThemeName: "catppuccin-mocha"
    readonly property string defaultIconThemeName: "Adwaita"
    readonly property string defaultSidebarPosition: "left"
    readonly property int defaultSidebarWidth: 200
    readonly property int defaultRadiusSmall: 4
    readonly property int defaultRadiusMedium: 8
    readonly property int defaultRadiusLarge: 12
    readonly property bool defaultTransparencyEnabled: true
    readonly property real defaultTransparencyLevel: 1.0
    readonly property bool defaultAnimationsEnabled: true
    readonly property int defaultAnimDurationFast: 100
    readonly property int defaultAnimDuration: 200
    readonly property int defaultAnimDurationSlow: 350
    readonly property string defaultAnimCurveEnter: "OutCubic"
    readonly property string defaultAnimCurveExit: "InCubic"
    readonly property string defaultAnimCurveTransition: "Bezier"
    readonly property bool defaultShowWindowControls: false
    readonly property string defaultWindowButtonLayout: ":minimize,maximize,close"

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
    property string draftSidebarPosition: config.sidebarPosition
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

    Component { id: paletteSectionIcon; IconSettings {} }
    Component { id: layoutSectionIcon; IconPanelLeft {} }
    Component { id: motionSectionIcon; IconClock {} }
    Component { id: toolsSectionIcon; IconFolder {} }

    property int currentSectionIndex: 0
    readonly property bool compactNavigation: dialogWidth < 860
    readonly property var sectionNavItems: [
        { title: "Look & Feel", iconComponent: paletteSectionIcon },
        { title: "Layout", iconComponent: layoutSectionIcon },
        { title: "Motion", iconComponent: motionSectionIcon },
        { title: "Tools", iconComponent: toolsSectionIcon }
    ]
    readonly property var sectionItems: [
        { title: "Look & Feel", subtitle: "Theme, typography, icons, and surface styling.", iconComponent: paletteSectionIcon },
        { title: "Layout", subtitle: "Sidebar behavior, file visibility, and toolbar controls.", iconComponent: layoutSectionIcon },
        { title: "Motion", subtitle: "Animation timing and easing across the interface.", iconComponent: motionSectionIcon },
        { title: "Tools", subtitle: "Shortcuts, remote locations, and config behavior.", iconComponent: toolsSectionIcon }
    ]

    function showSection(index) {
        currentSectionIndex = index
        if (sideTabs)
            sideTabs.currentIndex = index
        if (compactSectionNav)
            compactSectionNav.currentIndex = index
        if (contentFlick)
            contentFlick.contentY = 0
    }

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

    function bindAppearancePreview() {
        Theme.radiusSmall = Qt.binding(function() {
            return root.visible ? root.draftRadiusSmall : config.radiusSmall
        })
        Theme.radiusMedium = Qt.binding(function() {
            return root.visible ? root.draftRadiusMedium : config.radiusMedium
        })
        Theme.radiusLarge = Qt.binding(function() {
            return root.visible ? root.draftRadiusLarge : config.radiusLarge
        })
        Theme.transparencyEnabled = Qt.binding(function() {
            return root.visible ? root.draftTransparencyEnabled : config.transparencyEnabled
        })
        Theme.transparencyLevel = Qt.binding(function() {
            return root.visible ? root.draftTransparencyLevel : Math.max(0, Math.min(1, config.transparencyLevel))
        })
        Theme.animationsEnabled = Qt.binding(function() {
            return root.visible ? root.draftAnimationsEnabled : config.animationsEnabled
        })
    }

    function resetToDefaults() {
        setDraftTheme(defaultThemeName)
        draftFontFamily = ""
        draftIconTheme = defaultIconThemeName
        draftShowHidden = false
        draftSidebarVisible = true
        draftSidebarPosition = defaultSidebarPosition
        draftSidebarWidth = defaultSidebarWidth
        draftRadiusSmall = defaultRadiusSmall
        draftRadiusMedium = defaultRadiusMedium
        draftRadiusLarge = defaultRadiusLarge
        draftTransparencyEnabled = defaultTransparencyEnabled
        draftTransparencyLevel = defaultTransparencyLevel
        draftAnimationsEnabled = defaultAnimationsEnabled
        draftAnimDurationFast = defaultAnimDurationFast
        draftAnimDuration = defaultAnimDuration
        draftAnimDurationSlow = defaultAnimDurationSlow
        draftAnimCurveEnter = defaultAnimCurveEnter
        draftAnimCurveExit = defaultAnimCurveExit
        draftAnimCurveTransition = defaultAnimCurveTransition
        draftShowWindowControls = defaultShowWindowControls
        draftWindowButtonLayout = defaultWindowButtonLayout
        applySettingsNow()
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
            draftSidebarPosition = config.sidebarPosition
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
        showSection(0)
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
            sidebarPosition: draftSidebarPosition,
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
    Component.onCompleted: {
        root.primeOptionSources()
        root.bindAppearancePreview()
    }

    Timer {
        id: settingsApplyTimer
        interval: 140
        onTriggered: root.applyPendingSettings()
    }

    Component {
        id: lookPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                spacing: 12

                Text {
                    text: "Dark Mode"
                    color: Theme.text
                    font.pointSize: Theme.fontNormal + 2
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Q.Toggle {
                    label: ""
                    checked: root.draftDarkMode
                    onToggled: (value) => {
                        root.setDraftTheme(value ? "catppuccin-mocha" : "catppuccin-latte")
                        root.applySettingsNow()
                    }
                }
            }

            Q.Separator { Layout.bottomMargin: 8 }

            Text {
                text: "Theme"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.bottomMargin: 4
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
                label: "Icon Pack"
                model: root.iconThemeOptions
                currentIndex: root.optionIndex(root.iconThemeOptions, root.draftIconTheme, 0)
                onSelected: (_, value) => {
                    root.draftIconTheme = value
                    root.applySettingsNow()
                }
            }

            Text {
                text: "Surface Styling"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
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
        }
    }

    Component {
        id: layoutPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 6

            Text {
                text: "Browsing"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.bottomMargin: 4
            }

            Q.Checkbox {
                label: "Show hidden files"
                checked: root.draftShowHidden
                onToggled: (value) => {
                    root.draftShowHidden = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Show sidebar"
                checked: root.draftSidebarVisible
                onToggled: (value) => {
                    root.draftSidebarVisible = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Sidebar on right"
                enabled: root.draftSidebarVisible
                checked: root.draftSidebarPosition === "right"
                onToggled: (value) => {
                    root.draftSidebarPosition = value ? "right" : "left"
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

            Text {
                text: "Window Controls"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
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

            Q.Toggle {
                Layout.fillWidth: true
                label: "Buttons on left"
                enabled: root.draftShowWindowControls
                checked: root._layoutParts.side === "left"
                onToggled: (value) => {
                    root.rebuildButtonLayout(
                        value ? "left" : "right",
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

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 54
                radius: Theme.radiusMedium
                color: Theme.containerColor(Theme.surface, 0.22)
                border.width: 1
                border.color: root.sectionBorderColor
                opacity: root.draftShowWindowControls ? 1 : 0.6

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        visible: root._layoutParts.side === "left"
                        spacing: 6

                        Rectangle { visible: root._layoutParts.hasMinimize; width: 12; height: 12; radius: 6; color: Theme.warning }
                        Rectangle { visible: root._layoutParts.hasMaximize; width: 12; height: 12; radius: 6; color: Theme.success }
                        Rectangle { visible: root._layoutParts.hasClose; width: 12; height: 12; radius: 6; color: Theme.error }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 6
                        radius: 3
                        color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                    }

                    RowLayout {
                        visible: root._layoutParts.side !== "left"
                        spacing: 6

                        Rectangle { visible: root._layoutParts.hasMinimize; width: 12; height: 12; radius: 6; color: Theme.warning }
                        Rectangle { visible: root._layoutParts.hasMaximize; width: 12; height: 12; radius: 6; color: Theme.success }
                        Rectangle { visible: root._layoutParts.hasClose; width: 12; height: 12; radius: 6; color: Theme.error }
                    }
                }
            }
        }
    }

    Component {
        id: motionPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                spacing: 12

                Text {
                    text: "Animations"
                    color: Theme.text
                    font.pointSize: Theme.fontNormal + 2
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Q.Toggle {
                    label: ""
                    checked: root.draftAnimationsEnabled
                    onToggled: (value) => {
                        root.draftAnimationsEnabled = value
                        root.applySettingsNow()
                    }
                }
            }

            Q.Separator { Layout.bottomMargin: 8 }

            Text {
                text: "Timing"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.bottomMargin: 4
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
                text: "Curves"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
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

    Component {
        id: toolsPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 6

            Text {
                text: "Utilities"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.bottomMargin: 4
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Q.Button {
                    Layout.fillWidth: true
                    text: "Keyboard Shortcuts"
                    onClicked: root.openKeyboardShortcuts()
                }

                Q.Button {
                    Layout.fillWidth: true
                    text: "Connect to Network Location"
                    variant: "ghost"
                    onClicked: root.openRemoteConnect()
                }
            }
        }
    }

    Item {
        id: pageContainer
        Layout.fillWidth: true
        implicitHeight: Math.max(460, Math.min(pageContentHeight + 120, 640, root.height - 140))

        property real pageContentHeight: pageLoader.item ? pageLoader.item.implicitHeight : 0

        Behavior on implicitHeight {
            NumberAnimation {
                duration: Theme.animDuration
                easing.type: Easing.OutCubic
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: root.draftRadiusLarge + 6
            color: Theme.containerColor(Theme.mantle, 0.9)
            border.width: 1
            border.color: root.sectionBorderColor

            Behavior on radius {
                NumberAnimation {
                    duration: Theme.animDurationFast
                    easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
                }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Rectangle {
                        visible: !root.compactNavigation
                        Layout.fillHeight: true
                        Layout.preferredWidth: 184
                        color: Theme.containerColor(Theme.crust, 0.96)
                        radius: root.draftRadiusLarge + 6

                        Behavior on radius {
                            NumberAnimation {
                                duration: Theme.animDurationFast
                                easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
                            }
                        }

                        Rectangle {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: root.draftRadiusLarge + 6
                            color: parent.color

                            Behavior on width {
                                NumberAnimation {
                                    duration: Theme.animDurationFast
                                    easing.type: Theme.animEasingEnter; easing.bezierCurve: Theme.animBezierCurve
                                }
                            }
                        }

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            anchors.topMargin: 16
                            spacing: 2

                            Row {
                                Layout.leftMargin: 12
                                Layout.bottomMargin: 12
                                spacing: 6

                                IconSettings {
                                    size: 16
                                    color: Theme.text
                                }

                                Text {
                                    text: "Settings"
                                    color: Theme.text
                                    font.pointSize: Theme.fontNormal + 1
                                    font.bold: true
                                }
                            }

                            Q.Tabs {
                                id: sideTabs
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                orientation: Qt.Vertical
                                model: root.sectionNavItems
                                labelRole: "title"
                                iconComponentRole: "iconComponent"
                                currentIndex: root.currentSectionIndex
                                sideTabHeight: 36
                                sideTabWidth: 168
                                onTabChanged: (index) => root.showSection(index)
                            }

                            Q.Button {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 8
                                text: "Reset to Defaults"
                                variant: "ghost"
                                onClicked: root.resetToDefaults()
                            }
                        }
                    }

                    Q.Separator {
                        visible: !root.compactNavigation
                        orientation: Qt.Vertical
                        Layout.fillHeight: true
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 12

                            Q.Tabs {
                                id: compactSectionNav
                                visible: root.compactNavigation
                                Layout.fillWidth: true
                                model: root.sectionNavItems
                                labelRole: "title"
                                currentIndex: root.currentSectionIndex
                                onTabChanged: (index) => root.showSection(index)
                            }

                            Text {
                                text: root.sectionItems[root.currentSectionIndex].title
                                color: Theme.text
                                font.pointSize: Theme.fontLarge + 2
                                font.bold: true
                            }

                            Flickable {
                                id: contentFlick
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                contentWidth: width
                                contentHeight: pageLoader.item ? pageLoader.item.implicitHeight : 0
                                boundsBehavior: Flickable.StopAtBounds
                                interactive: contentHeight > height

                                Loader {
                                    id: pageLoader
                                    width: contentFlick.width
                                    sourceComponent: root.currentSectionIndex === 0
                                        ? lookPageComponent
                                        : root.currentSectionIndex === 1
                                            ? layoutPageComponent
                                            : root.currentSectionIndex === 2
                                                ? motionPageComponent
                                                : toolsPageComponent
                                }
                            }
                        }
                    }
                }

            }
        }
    }
}
