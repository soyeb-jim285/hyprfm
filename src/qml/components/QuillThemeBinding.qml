import QtQuick
import QtQuick.Window
import HyprFM
import Quill as Q

// Bridges the HyprFM theme into Quill's singleton. Every QML root window that
// uses Quill controls needs one. An Item so Screen resolves to its window.
Item {
    Component.onCompleted: {
        Q.Theme.background = Qt.binding(() => Theme.base)
        Q.Theme.backgroundAlt = Qt.binding(() => Theme.mantle)
        Q.Theme.backgroundDeep = Qt.binding(() => Theme.crust)
        Q.Theme.surface0 = Qt.binding(() => Theme.surface)
        // surface1/surface2 back Quill components that need an opaque fill
        // (Tooltip, Popup, etc.), so pre-composite the 10%/15% text tint onto
        // the base background instead of emitting a translucent color.
        Q.Theme.surface1 = Qt.binding(() => Qt.rgba(
            Theme.base.r * 0.9 + Theme.text.r * 0.1,
            Theme.base.g * 0.9 + Theme.text.g * 0.1,
            Theme.base.b * 0.9 + Theme.text.b * 0.1,
            1.0))
        Q.Theme.surface2 = Qt.binding(() => Qt.rgba(
            Theme.base.r * 0.85 + Theme.text.r * 0.15,
            Theme.base.g * 0.85 + Theme.text.g * 0.15,
            Theme.base.b * 0.85 + Theme.text.b * 0.15,
            1.0))
        Q.Theme.textPrimary = Qt.binding(() => Theme.text)
        Q.Theme.textSecondary = Qt.binding(() => Theme.subtext)
        Q.Theme.textTertiary = Qt.binding(() => Theme.muted)
        Q.Theme.primary = Qt.binding(() => Theme.accent)
        Q.Theme.success = Qt.binding(() => Theme.success)
        Q.Theme.warning = Qt.binding(() => Theme.warning)
        Q.Theme.error = Qt.binding(() => Theme.error)
        Q.Theme.radiusSm = Qt.binding(() => Theme.radiusSmall)
        Q.Theme.radius = Qt.binding(() => Theme.radiusMedium)
        Q.Theme.radiusLg = Qt.binding(() => Theme.radiusLarge)
        Q.Theme.fontFamily = Qt.binding(() => Qt.application.font.family)
        // Theme sizes are points; Quill sets font.pixelSize.
        const pointsToPixels = points => Math.round(points * Screen.logicalPixelDensity * 25.4 / 72)
        Q.Theme.fontSizeSmall = Qt.binding(() => pointsToPixels(Theme.fontSmall))
        Q.Theme.fontSize = Qt.binding(() => pointsToPixels(Theme.fontNormal))
        Q.Theme.fontSizeLarge = Qt.binding(() => pointsToPixels(Theme.fontLarge))
        Q.Theme.animDurationFast = Qt.binding(() => Theme.animDurationFast)
        Q.Theme.animDuration = Qt.binding(() => Theme.animDuration)
        Q.Theme.animDurationSlow = Qt.binding(() => Theme.animDurationSlow)
        Q.Theme.transparencyEnabled = Qt.binding(() => Theme.transparencyEnabled)
        Q.Theme.transparencyLevel = Qt.binding(() => Theme.transparencyLevel)
    }
}
