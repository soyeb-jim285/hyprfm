import QtQuick
import QtQuick.Layouts

// One pane's built-in terminal: a TerminalPanel under that pane's file view.
// Each pane has its own, so split panes in different folders each get a shell
// in their own folder. Created on first open and kept while hidden, so its
// shells survive F4; it goes when its last shell exits, or with its pane.
// TerminalPanel.qml is only in the module when HyprFM is built with
// HYPRFM_TERMINAL, hence loading it by URL.
Loader {
    id: holder

    // "primary" or "secondary".
    required property string pane
    property bool open: false
    property bool created: false
    // The panel never grows past 70% of its pane.
    property real paneHeight: 0
    property var windowShortcuts: []

    // The shell of the panel's front tab.
    readonly property var terminal: item ? item.terminal : null

    signal finished()
    signal unsafePaste(var view, string text)
    signal folderActivated(string path)

    objectName: pane + "PaneTerminal"
    Layout.fillWidth: true
    Layout.preferredHeight: item ? item.implicitHeight : 0
    visible: open
    active: created
    source: Qt.resolvedUrl("TerminalPanel.qml")
    onLoaded: {
        item.maximumHeight = Qt.binding(() => Math.max(80, holder.paneHeight * 0.7))
        item.windowShortcuts = Qt.binding(() => holder.windowShortcuts)
    }

    Connections {
        target: holder.item
        function onFinished() { holder.finished() }
        function onUnsafePaste(view, text) { holder.unsafePaste(view, text) }
        // The view only passes on web, mail and local folder links; see
        // TerminalView::activateLink().
        function onLinkActivated(url) { Qt.openUrlExternally(url) }
        function onFolderActivated(path) { holder.folderActivated(path) }
    }
}
