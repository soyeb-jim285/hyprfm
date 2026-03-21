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

    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 8
    readonly property int radiusLarge: 12
    readonly property int spacing: 8
    readonly property int fontSmall: 11
    readonly property int fontNormal: 13
    readonly property int fontLarge: 16
}
