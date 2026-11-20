import QtQuick
import HyprFM

// Shared by the sidebar and both file panes. Preview magnification and icon
// density keep their own state; interface text size is a saved preference.
// Ctrl+Wheel zooms the file view under the pointer, that pane only, as other
// file managers do (Grid icons, or row height in Compact, Detailed and
// Miller); over the sidebar or the Computer page it does nothing.
// Ctrl+Shift+Wheel sizes interface text for the whole window.
Item {
    id: root
    property Item previewControls: null
    property Item fileView: null
    property bool viewZoomEnabled: false
    property real pendingSteps: 0
    property int previousDirection: 0

    function adjust(steps) {
        config.saveSettings({ fontSize: Math.max(8, Math.min(24, Theme.baseFontSize + steps)) })
    }
    function reset() { config.saveSettings({ fontSize: 0 }) }

    function handleWheel(wheel) {
        if (previewControls && previewControls.scope && previewControls.scope.visible) {
            const point = previewControls.scope.mapFromItem(root, wheel.x, wheel.y)
            if (point.x >= 0 && point.y >= 0 && point.x < previewControls.scope.width && point.y < previewControls.scope.height) {
                if (previewControls.available) previewControls.wheelZoom(wheel)
                return true
            }
        }
        // (Shift turns a vertical wheel into a horizontal one on some systems)
        const shift = !!(wheel.modifiers & Qt.ShiftModifier)
        const pixels = wheel.pixelDelta.y || (shift ? wheel.pixelDelta.x : 0)
        const angle = wheel.angleDelta.y || (shift ? wheel.angleDelta.x : 0)
        const steps = (pixels ? pixels / 10 : angle / 120) * (wheel.inverted ? -1 : 1)
        if (!steps) return false
        if (!shift) {
            if (!viewZoomEnabled || !fileView || !fileView.zoomByWheel) return false
            fileView.zoomByWheel(-steps * 42)
            return true
        }
        const direction = steps > 0 ? 1 : -1
        if (direction !== previousDirection) pendingSteps = 0
        previousDirection = direction
        pendingSteps += steps
        const whole = Math.trunc(pendingSteps)
        if (whole) { pendingSteps -= whole; adjust(whole) }
        return true
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.ControlModifier
        onWheel: wheel => { blocking = root.handleWheel(wheel) }
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.ControlModifier | Qt.ShiftModifier
        onWheel: wheel => { blocking = root.handleWheel(wheel) }
    }
    WheelHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        acceptedModifiers: Qt.ControlModifier | Qt.ShiftModifier
        orientation: Qt.Horizontal
        onWheel: wheel => { blocking = root.handleWheel(wheel) }
    }
}
