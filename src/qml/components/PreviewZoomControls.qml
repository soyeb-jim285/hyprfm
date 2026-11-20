import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM
import Quill as Q

Item {
    id: root
    property Item scope: null
    property bool available: true
    property Item scrollTarget: null
    property var pageWheel: null
    property real factor: 1.0
    readonly property real minimum: 0.25
    readonly property real maximum: 4.0
    signal interacted()
    onFactorChanged: zoomSlider.value = factor
    implicitHeight: Theme.controlSize + 8
    height: available ? implicitHeight : 0
    visible: available

    function setFactor(value) {
        factor = Math.max(minimum, Math.min(maximum, isFinite(value) ? value : 1))
    }
    function reset() { setFactor(1) }
    // One wheel notch or one Ctrl++ / Ctrl+- press.
    function zoomSteps(steps) { setFactor(factor * Math.exp(steps * 0.14)) }
    function focusPreview() {
        interacted()
        Qt.callLater(function() { if (root.scope && root.scope.visible && !root.scope.activeFocus) root.scope.forceActiveFocus() })
    }
    function wheelZoom(wheel) {
        var steps = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y / 10 : wheel.angleDelta.y / 120
        if (steps === 0) return false
        zoomSteps(steps * (wheel.inverted ? -1 : 1))
        focusPreview()
        return true
    }

    function scrollContent(wheel) {
        if (pageWheel && factor <= 1) { pageWheel(wheel); return true }
        if (!scrollTarget) return false
        var dx = wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : wheel.angleDelta.x / 120 * 42
        var dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 120 * 42
        if (wheel.modifiers & Qt.ShiftModifier) { dx = dx || dy; dy = 0 }
        if (wheel.inverted) { dx = -dx; dy = -dy }
        scrollTarget.contentX = Math.max(0, Math.min(scrollTarget.contentWidth - scrollTarget.width, scrollTarget.contentX - dx))
        scrollTarget.contentY = Math.max(0, Math.min(scrollTarget.contentHeight - scrollTarget.height, scrollTarget.contentY - dy))
        return dx !== 0 || dy !== 0
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6
        Q.Slider {
            id: zoomSlider
            objectName: "previewZoomSlider"
            Layout.fillWidth: true
            from: root.minimum
            to: root.maximum
            stepSize: 0.05
            decimals: 2
            activeFocusOnTab: true
            value: root.factor
            Accessible.name: qsTr("Preview zoom")
            onMoved: value => { root.setFactor(value); root.focusPreview() }
            Keys.onLeftPressed: event => { root.setFactor(root.factor - 0.05); event.accepted = true }
            Keys.onRightPressed: event => { root.setFactor(root.factor + 0.05); event.accepted = true }
        }
        Text {
            text: Math.round(root.factor * 100) + "%"
            color: Theme.subtext
            font.pointSize: Theme.fontSmall
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 42 * Theme.uiScale
        }
        HoverRect {
            objectName: "resetPreviewZoom"
            width: Theme.controlSize
            height: Theme.controlSize
            tooltip: qsTr("Reset preview zoom")
            Accessible.role: Accessible.Button
            onClicked: { root.reset(); root.focusPreview() }
            IconUndo { anchors.centerIn: parent; size: 16; color: Theme.text }
        }
    }
    // The interface text-size keys act on the preview while it has focus;
    // Main.qml's text-size shortcuts stand down then (previewOwnsFocus()).
    readonly property bool keysActive: root.available && !!root.scope && root.scope.visible && root.scope.activeFocus
    Shortcut {
        sequences: [config.shortcutMap["text_larger"], "Ctrl+="]
        enabled: root.keysActive
        onActivated: root.zoomSteps(1)
    }
    Shortcut {
        sequence: config.shortcutMap["text_smaller"]
        enabled: root.keysActive
        onActivated: root.zoomSteps(-1)
    }
    Shortcut {
        sequence: config.shortcutMap["text_reset"]
        enabled: root.keysActive
        onActivated: root.reset()
    }
    Item {
        // Input stays local to this preview; ordinary scrolling and other
        // modifiers continue to its content instead of changing file-view zoom.
        parent: root.scope ? root.scope : root
        anchors.fill: parent
        z: 100
        enabled: root.available
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ControlModifier
            onWheel: wheel => { blocking = root.wheelZoom(wheel) }
        }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ControlModifier | Qt.ShiftModifier
            onWheel: wheel => { blocking = root.wheelZoom(wheel) }
        }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            onWheel: wheel => { blocking = root.scrollContent(wheel) }
        }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.NoModifier
            orientation: Qt.Horizontal
            onWheel: wheel => { blocking = root.scrollContent(wheel) }
        }
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            acceptedModifiers: Qt.ShiftModifier
            onWheel: wheel => { blocking = root.scrollContent(wheel) }
        }
    }
}
