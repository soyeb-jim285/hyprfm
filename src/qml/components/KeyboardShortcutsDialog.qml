import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1001
    dialogWidth: Math.min(760, width - 40)
    title: "Keyboard Shortcuts"
    subtitle: "Edit shortcut text directly. Clear a field to fall back to the built-in default."

    property var shortcutEntries: []
    property var draftShortcuts: ({})
    property bool syncingFromConfig: false
    property bool pendingShortcutsDirty: false

    function syncShortcutDrafts() {
        syncingFromConfig = true
        try {
            shortcutEntries = config.shortcutDefinitions

            var nextShortcuts = ({})
            for (var i = 0; i < shortcutEntries.length; ++i) {
                var entry = shortcutEntries[i]
                nextShortcuts[entry.action] = entry.sequence
            }
            draftShortcuts = nextShortcuts
        } finally {
            syncingFromConfig = false
        }
    }

    function setShortcutValue(action, value) {
        var nextShortcuts = ({})
        for (var key in draftShortcuts)
            nextShortcuts[key] = draftShortcuts[key]
        nextShortcuts[action] = value
        draftShortcuts = nextShortcuts

        var nextEntries = []
        for (var i = 0; i < shortcutEntries.length; ++i) {
            var entry = shortcutEntries[i]
            if (entry.action === action) {
                var updatedEntry = ({})
                for (var field in entry)
                    updatedEntry[field] = entry[field]
                updatedEntry.sequence = value
                nextEntries.push(updatedEntry)
            } else {
                nextEntries.push(entry)
            }
        }
        shortcutEntries = nextEntries
    }

    function queueShortcutApply() {
        if (syncingFromConfig)
            return

        pendingShortcutsDirty = true
        shortcutApplyTimer.restart()
    }

    function applyPendingShortcuts() {
        if (!pendingShortcutsDirty)
            return

        pendingShortcutsDirty = false
        shortcutApplyTimer.stop()
        config.saveShortcuts(draftShortcuts)
    }

    function openDialog() {
        syncShortcutDrafts()
        open()
    }

    function closeDialog() {
        applyPendingShortcuts()
        close()
    }

    onRejected: root.applyPendingShortcuts()
    onClosed: root.applyPendingShortcuts()

    Timer {
        id: shortcutApplyTimer
        interval: 320
        onTriggered: root.applyPendingShortcuts()
    }

    Item {
        Layout.fillWidth: true
        implicitHeight: Math.min(540, shortcutsFlick.contentHeight)

        Flickable {
            id: shortcutsFlick
            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: shortcutsColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: shortcutsColumn
                width: shortcutsFlick.width
                spacing: 8

                Repeater {
                    model: root.shortcutEntries

                    delegate: Rectangle {
                        required property var modelData

                        width: shortcutsColumn.width
                        implicitHeight: shortcutColumn.implicitHeight + 16
                        radius: Theme.radiusLarge
                        color: Theme.containerColor(Theme.crust, 0.32)
                        border.width: 1
                        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                        ColumnLayout {
                            id: shortcutColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10

                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    color: Theme.text
                                    font.pointSize: Theme.fontNormal
                                    font.bold: true
                                }

                                Q.TextField {
                                    Layout.preferredWidth: 180
                                    Layout.maximumWidth: 210
                                    variant: "filled"
                                    placeholder: modelData.defaultSequence
                                    text: modelData.sequence
                                    onTextEdited: {
                                        root.setShortcutValue(modelData.action, text)
                                        root.queueShortcutApply()
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: "Default: " + modelData.defaultSequence
                                color: Theme.subtext
                                font.pointSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: "Shortcut edits save automatically while you type."
            color: Theme.subtext
            font.pointSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        Q.Button {
            text: "Done"
            onClicked: root.closeDialog()
        }
    }
}
