pragma Singleton
import QtQuick

QtObject {
    id: root

    property color base: theme.base
    property color mantle: theme.mantle
    property color crust: theme.crust
    property color surface: theme.surface
    property color overlay: theme.overlay
    property color text: theme.text
    property color subtext: theme.subtext
    property color muted: theme.muted
    property color accent: theme.accent
    property color success: theme.success
    property color warning: theme.warning
    property color error: theme.error

    property int radiusSmall: config.radiusSmall
    property int radiusMedium: config.radiusMedium
    property int radiusLarge: config.radiusLarge
    readonly property real baseFontSize: {
        var pointSize = Qt.application.font.pointSize
        return pointSize > 0 ? pointSize : 10
    }
    readonly property real uiScale: Math.max(1.0, baseFontSize / 10.0)
    readonly property int spacing: Math.round(8 * uiScale)
    readonly property int fontSmall: Math.max(9, Math.round(baseFontSize - 1))
    readonly property int fontNormal: Math.max(10, Math.round(baseFontSize))
    readonly property int fontLarge: Math.max(12, Math.round(baseFontSize + 2))
    readonly property int controlSize: Math.round(32 * uiScale)
    readonly property int compactControlSize: Math.round(28 * uiScale)
    readonly property int titleBarHeight: Math.round(34 * uiScale)
    readonly property int toolbarRowHeight: Math.round(44 * uiScale)
    property bool transparencyEnabled: config.transparencyEnabled
    property real transparencyLevel: Math.max(0, Math.min(1, config.transparencyLevel))
    property bool animationsEnabled: config.animationsEnabled

    readonly property int animDurationFast: animationsEnabled ? 100 : 0
    readonly property int animDuration: animationsEnabled ? 200 : 0
    readonly property int animDurationSlow: animationsEnabled ? 350 : 0

    Behavior on base {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on mantle {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on crust {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on surface {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on overlay {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on text {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on subtext {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on muted {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on accent {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on success {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on warning {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on error {
        ColorAnimation { duration: root.animDurationSlow; easing.type: Easing.InOutCubic }
    }

    Behavior on radiusSmall {
        NumberAnimation { duration: root.animDurationFast; easing.type: Easing.OutCubic }
    }

    Behavior on radiusMedium {
        NumberAnimation { duration: root.animDurationFast; easing.type: Easing.OutCubic }
    }

    Behavior on radiusLarge {
        NumberAnimation { duration: root.animDurationFast; easing.type: Easing.OutCubic }
    }

    Behavior on transparencyLevel {
        NumberAnimation { duration: root.animDuration; easing.type: Easing.OutCubic }
    }

    function containerColor(color, defaultAlpha) {
        var strength = transparencyEnabled ? transparencyLevel : 0
        var alpha = 1 - strength * (1 - defaultAlpha)
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }
}
