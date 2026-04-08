pragma Singleton
import QtQuick

QtObject {
    readonly property color base: theme.base
    readonly property color mantle: theme.mantle
    readonly property color crust: theme.crust
    readonly property color surface: theme.surface
    readonly property color overlay: theme.overlay
    readonly property color text: theme.text
    readonly property color subtext: theme.subtext
    readonly property color muted: theme.muted
    readonly property color accent: theme.accent
    readonly property color success: theme.success
    readonly property color warning: theme.warning
    readonly property color error: theme.error

    readonly property int radiusSmall: config.radiusSmall
    readonly property int radiusMedium: config.radiusMedium
    readonly property int radiusLarge: config.radiusLarge
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
    readonly property bool transparencyEnabled: config.transparencyEnabled
    readonly property real transparencyLevel: Math.max(0, Math.min(1, config.transparencyLevel))
    readonly property bool animationsEnabled: config.animationsEnabled

    readonly property int animDurationFast: animationsEnabled ? 100 : 0
    readonly property int animDuration: animationsEnabled ? 200 : 0
    readonly property int animDurationSlow: animationsEnabled ? 350 : 0

    function containerColor(color, defaultAlpha) {
        var strength = transparencyEnabled ? transparencyLevel : 0
        var alpha = 1 - strength * (1 - defaultAlpha)
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }
}
