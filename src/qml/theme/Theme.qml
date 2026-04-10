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

    readonly property int animDurationFast: animationsEnabled ? config.animDurationFast : 0
    readonly property int animDuration: animationsEnabled ? config.animDuration : 0
    readonly property int animDurationSlow: animationsEnabled ? config.animDurationSlow : 0
    function _curveToEasing(name) {
        switch (name) {
        case "Linear":       return Easing.Linear
        case "InCubic":      return Easing.InCubic
        case "OutCubic":     return Easing.OutCubic
        case "InOutCubic":   return Easing.InOutCubic
        case "OutBack":      return Easing.OutBack
        case "InOutQuad":    return Easing.InOutQuad
        case "OutQuad":      return Easing.OutQuad
        case "OutExpo":      return Easing.OutExpo
        case "InOutExpo":    return Easing.InOutExpo
        case "Bezier":       return Easing.BezierSpline
        default:             return Easing.OutCubic
        }
    }

    readonly property int animEasingEnter: _curveToEasing(config.animCurveEnter)
    readonly property int animEasingExit: _curveToEasing(config.animCurveExit)
    readonly property int animEasingTransition: _curveToEasing(config.animCurveTransition)
    // Material-design standard easing curve. Used when any of the above
    // resolves to Easing.BezierSpline — every Behavior that consumes one
    // of the animEasing* properties also sets easing.bezierCurve to this.
    readonly property var animBezierCurve: [0.4, 0.0, 0.2, 1.0, 1.0, 1.0]

    Behavior on base {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on mantle {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on crust {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on surface {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on overlay {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on text {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on subtext {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on muted {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on accent {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on success {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on warning {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on error {
        ColorAnimation { duration: root.animDurationSlow; easing.type: root.animEasingTransition; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on radiusSmall {
        NumberAnimation { duration: root.animDurationFast; easing.type: root.animEasingEnter; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on radiusMedium {
        NumberAnimation { duration: root.animDurationFast; easing.type: root.animEasingEnter; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on radiusLarge {
        NumberAnimation { duration: root.animDurationFast; easing.type: root.animEasingEnter; easing.bezierCurve: root.animBezierCurve }
    }

    Behavior on transparencyLevel {
        NumberAnimation { duration: root.animDuration; easing.type: root.animEasingEnter; easing.bezierCurve: root.animBezierCurve }
    }

    function containerColor(color, defaultAlpha) {
        var strength = transparencyEnabled ? transparencyLevel : 0
        var alpha = 1 - strength * (1 - defaultAlpha)
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }
}
