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
    readonly property int spacing: 8
    readonly property int fontSmall: 9
    readonly property int fontNormal: 10
    readonly property int fontLarge: 12
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
