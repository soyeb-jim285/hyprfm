import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1001
    dialogWidth: Math.min(640, width - 40)
    title: "Keyboard Shortcuts"
    subtitle: "Click a shortcut to rebind it, then press the new key combination."

    property var shortcutEntries: []
    property var draftShortcuts: ({})
    property bool syncingFromConfig: false
    property bool pendingShortcutsDirty: false
    property string recordingAction: ""

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
        recordingAction = ""
        syncShortcutDrafts()
        open()
    }

    function closeDialog() {
        recordingAction = ""
        applyPendingShortcuts()
        close()
    }

    function startRecording(action) {
        recordingAction = action
        keyCapture.forceActiveFocus()
    }

    function stopRecording() {
        recordingAction = ""
    }

    function resetToDefault(action) {
        for (var i = 0; i < shortcutEntries.length; ++i) {
            if (shortcutEntries[i].action === action) {
                setShortcutValue(action, shortcutEntries[i].defaultSequence)
                queueShortcutApply()
                return
            }
        }
    }

    function keyEventToSequence(event) {
        var parts = []

        if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
        if (event.modifiers & Qt.AltModifier) parts.push("Alt")
        if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
        if (event.modifiers & Qt.MetaModifier) parts.push("Meta")

        // Ignore bare modifier presses
        var key = event.key
        if (key === Qt.Key_Control || key === Qt.Key_Alt
            || key === Qt.Key_Shift || key === Qt.Key_Meta
            || key === Qt.Key_Super_L || key === Qt.Key_Super_R)
            return ""

        var keyName = ""
        var keyMap = {}
        keyMap[Qt.Key_A] = "A"; keyMap[Qt.Key_B] = "B"; keyMap[Qt.Key_C] = "C"
        keyMap[Qt.Key_D] = "D"; keyMap[Qt.Key_E] = "E"; keyMap[Qt.Key_F] = "F"
        keyMap[Qt.Key_G] = "G"; keyMap[Qt.Key_H] = "H"; keyMap[Qt.Key_I] = "I"
        keyMap[Qt.Key_J] = "J"; keyMap[Qt.Key_K] = "K"; keyMap[Qt.Key_L] = "L"
        keyMap[Qt.Key_M] = "M"; keyMap[Qt.Key_N] = "N"; keyMap[Qt.Key_O] = "O"
        keyMap[Qt.Key_P] = "P"; keyMap[Qt.Key_Q] = "Q"; keyMap[Qt.Key_R] = "R"
        keyMap[Qt.Key_S] = "S"; keyMap[Qt.Key_T] = "T"; keyMap[Qt.Key_U] = "U"
        keyMap[Qt.Key_V] = "V"; keyMap[Qt.Key_W] = "W"; keyMap[Qt.Key_X] = "X"
        keyMap[Qt.Key_Y] = "Y"; keyMap[Qt.Key_Z] = "Z"
        keyMap[Qt.Key_0] = "0"; keyMap[Qt.Key_1] = "1"; keyMap[Qt.Key_2] = "2"
        keyMap[Qt.Key_3] = "3"; keyMap[Qt.Key_4] = "4"; keyMap[Qt.Key_5] = "5"
        keyMap[Qt.Key_6] = "6"; keyMap[Qt.Key_7] = "7"; keyMap[Qt.Key_8] = "8"
        keyMap[Qt.Key_9] = "9"
        keyMap[Qt.Key_F1] = "F1"; keyMap[Qt.Key_F2] = "F2"; keyMap[Qt.Key_F3] = "F3"
        keyMap[Qt.Key_F4] = "F4"; keyMap[Qt.Key_F5] = "F5"; keyMap[Qt.Key_F6] = "F6"
        keyMap[Qt.Key_F7] = "F7"; keyMap[Qt.Key_F8] = "F8"; keyMap[Qt.Key_F9] = "F9"
        keyMap[Qt.Key_F10] = "F10"; keyMap[Qt.Key_F11] = "F11"; keyMap[Qt.Key_F12] = "F12"
        keyMap[Qt.Key_Space] = "Space"; keyMap[Qt.Key_Return] = "Return"
        keyMap[Qt.Key_Enter] = "Return"; keyMap[Qt.Key_Escape] = "Escape"
        keyMap[Qt.Key_Tab] = "Tab"; keyMap[Qt.Key_Backspace] = "Backspace"
        keyMap[Qt.Key_Delete] = "Delete"; keyMap[Qt.Key_Insert] = "Insert"
        keyMap[Qt.Key_Home] = "Home"; keyMap[Qt.Key_End] = "End"
        keyMap[Qt.Key_PageUp] = "PageUp"; keyMap[Qt.Key_PageDown] = "PageDown"
        keyMap[Qt.Key_Up] = "Up"; keyMap[Qt.Key_Down] = "Down"
        keyMap[Qt.Key_Left] = "Left"; keyMap[Qt.Key_Right] = "Right"
        keyMap[Qt.Key_Plus] = "+"; keyMap[Qt.Key_Minus] = "-"
        keyMap[Qt.Key_Equal] = "="; keyMap[Qt.Key_BracketLeft] = "["
        keyMap[Qt.Key_BracketRight] = "]"; keyMap[Qt.Key_Semicolon] = ";"
        keyMap[Qt.Key_Apostrophe] = "'"; keyMap[Qt.Key_Comma] = ","
        keyMap[Qt.Key_Period] = "."; keyMap[Qt.Key_Slash] = "/"
        keyMap[Qt.Key_Backslash] = "\\"; keyMap[Qt.Key_QuoteLeft] = "`"

        if (key in keyMap)
            keyName = keyMap[key]
        else
            keyName = event.text.toUpperCase()

        if (!keyName)
            return ""

        parts.push(keyName)
        return parts.join("+")
    }

    onRejected: { root.recordingAction = ""; root.applyPendingShortcuts() }
    onClosed: { root.recordingAction = ""; root.applyPendingShortcuts() }

    Timer {
        id: shortcutApplyTimer
        interval: 320
        onTriggered: root.applyPendingShortcuts()
    }

    // Invisible item that captures key presses while recording
    Item {
        id: keyCapture
        focus: root.recordingAction !== ""
        Keys.onPressed: (event) => {
            if (root.recordingAction === "")
                return

            // Escape cancels recording
            if (event.key === Qt.Key_Escape) {
                root.stopRecording()
                event.accepted = true
                return
            }

            var seq = root.keyEventToSequence(event)
            if (seq !== "") {
                root.setShortcutValue(root.recordingAction, seq)
                root.queueShortcutApply()
                root.stopRecording()
                event.accepted = true
            }
        }
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

            ColumnLayout {
                id: shortcutsColumn
                width: shortcutsFlick.width
                spacing: 0

                // Header row
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 36
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.04)
                    radius: Theme.radiusSmall

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        spacing: 8

                        Text {
                            Layout.fillWidth: true
                            text: "Action"
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                        }

                        Text {
                            Layout.preferredWidth: 180
                            text: "Shortcut"
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            font.weight: Font.DemiBold
                        }

                        Item { width: 28 }
                    }
                }

                Repeater {
                    model: root.shortcutEntries

                    delegate: Rectangle {
                        id: shortcutRow
                        required property var modelData
                        required property int index

                        readonly property bool isRecording: root.recordingAction === modelData.action
                        readonly property bool isModified: modelData.sequence !== modelData.defaultSequence

                        Layout.fillWidth: true
                        implicitHeight: 44
                        color: {
                            if (isRecording)
                                return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                            if (rowHover.hovered)
                                return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.04)
                            return "transparent"
                        }
                        Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                        // Bottom separator
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            height: 1
                            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                            visible: shortcutRow.index < root.shortcutEntries.length - 1
                        }

                        HoverHandler {
                            id: rowHover
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 8

                            // Action label
                            Text {
                                Layout.fillWidth: true
                                text: shortcutRow.modelData.label
                                color: Theme.text
                                font.pointSize: Theme.fontNormal
                            }

                            // Shortcut badge / recording indicator
                            Rectangle {
                                Layout.preferredWidth: 180
                                Layout.preferredHeight: 30
                                radius: Theme.radiusSmall
                                color: {
                                    if (shortcutRow.isRecording)
                                        return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.2)
                                    return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                                }
                                border.width: shortcutRow.isRecording ? 1 : 0
                                border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.6)

                                Behavior on color { ColorAnimation { duration: Theme.animDuration } }
                                Behavior on border.width { NumberAnimation { duration: Theme.animDuration } }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 4

                                    Text {
                                        Layout.fillWidth: true
                                        text: shortcutRow.isRecording
                                            ? "Press keys..."
                                            : shortcutRow.modelData.sequence
                                        color: shortcutRow.isRecording ? Theme.accent : Theme.text
                                        font.pointSize: Theme.fontNormal
                                        font.weight: Font.Medium
                                        font.italic: shortcutRow.isRecording
                                        elide: Text.ElideRight

                                        SequentialAnimation on opacity {
                                            running: shortcutRow.isRecording
                                            loops: Animation.Infinite
                                            NumberAnimation { to: 0.4; duration: 600; easing.type: Easing.InOutSine }
                                            NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
                                        }
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        if (shortcutRow.isRecording)
                                            root.stopRecording()
                                        else
                                            root.startRecording(shortcutRow.modelData.action)
                                    }
                                }
                            }

                            // Reset button (visible when modified)
                            HoverRect {
                                width: 28; height: 28
                                visible: shortcutRow.isModified && !shortcutRow.isRecording
                                opacity: visible ? 1 : 0
                                Behavior on opacity { NumberAnimation { duration: Theme.animDuration } }
                                onClicked: root.resetToDefault(shortcutRow.modelData.action)

                                IconUndo {
                                    anchors.centerIn: parent
                                    size: 14
                                    color: Theme.subtext
                                }
                            }

                            // Spacer when reset button is hidden
                            Item {
                                width: 28
                                visible: !shortcutRow.isModified || shortcutRow.isRecording
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
            text: root.recordingAction !== ""
                ? "Press Escape to cancel recording."
                : "Click a shortcut to change it. Changes save automatically."
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
