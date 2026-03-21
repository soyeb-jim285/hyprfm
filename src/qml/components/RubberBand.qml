import QtQuick
import HyprFM

// Semi-transparent rubber-band selection rectangle.
// Usage:
//   rubberBand.begin(Qt.point(x, y))
//   rubberBand.update(Qt.point(x, y))
//   rubberBand.end()
//
// Read selectionRect (Qt.rect) to query the current selection area.
Item {
    id: root

    anchors.fill: parent
    visible: false

    // The normalised selection rectangle in parent coordinates
    readonly property rect selectionRect: Qt.rect(
        Math.min(_startX, _endX),
        Math.min(_startY, _endY),
        Math.abs(_endX - _startX),
        Math.abs(_endY - _startY)
    )

    property real _startX: 0
    property real _startY: 0
    property real _endX: 0
    property real _endY: 0

    // ── Public API ────────────────────────────────────────────────────────────

    function begin(pos) {
        _startX = pos.x
        _startY = pos.y
        _endX   = pos.x
        _endY   = pos.y
        root.visible = false   // don't show yet (wait for some movement)
    }

    function update(pos) {
        _endX = pos.x
        _endY = pos.y
        // Show once we have a minimal drag distance
        if (Math.abs(_endX - _startX) > 4 || Math.abs(_endY - _startY) > 4)
            root.visible = true
    }

    function end() {
        root.visible = false
        _startX = 0; _startY = 0; _endX = 0; _endY = 0
    }

    // ── Visual ────────────────────────────────────────────────────────────────
    Rectangle {
        x:      root.selectionRect.x
        y:      root.selectionRect.y
        width:  root.selectionRect.width
        height: root.selectionRect.height

        color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.10)
        border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.7)
        border.width: 1
        radius: 2
    }
}
