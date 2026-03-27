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

    readonly property int animDurationFast: 100
    readonly property int animDuration: 200
    readonly property int animDurationSlow: 350
}
