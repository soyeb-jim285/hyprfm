import QtQuick
import HyprFM

// The text field a file view's delegate puts over its name while that entry
// is being named in place (see InlineEditState). Enter or Tab applies,
// Escape cancels, focus moving elsewhere in the window applies. A file's
// name is preselected up to its extension, a folder's in full.
Rectangle {
    id: editor
    property var state: null
    property string path: ""
    property bool isDir: false
    property int horizontalAlignment: TextInput.AlignLeft
    readonly property bool editing: !!state && state.active && state.path === path && path !== ""
    readonly property alias input: field

    visible: editing
    z: 50
    implicitHeight: field.implicitHeight + 6
    radius: Theme.radiusSmall
    color: Theme.base
    border.width: 1
    border.color: state && state.error !== "" ? Theme.error : Theme.accent

    function start() {
        if (!editing) return
        field.text = state.text
        field.forceActiveFocus()
        const dot = isDir ? -1 : field.text.lastIndexOf(".")
        if (dot > 0) field.select(0, dot)
        else field.selectAll()
    }
    // Ending the edit drops the field's focus too; the view (a focus scope)
    // would otherwise hand active focus back to this hidden field.
    onEditingChanged: {
        if (editing) Qt.callLater(start)
        else field.focus = false
    }
    // A delegate recreated mid-edit (scrolling, a refresh) picks the edit up.
    Component.onCompleted: if (editing) Qt.callLater(start)

    TextInput {
        id: field
        objectName: "inlineNameInput"
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        verticalAlignment: TextInput.AlignVCenter
        horizontalAlignment: editor.horizontalAlignment
        clip: true
        selectByMouse: true
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.base
        font.pointSize: Theme.fontNormal
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("Name")

        onTextEdited: {
            if (!editor.state) return
            editor.state.text = text
            editor.state.error = ""
        }
        onActiveFocusChanged: if (editor.state && editor.editing) editor.state.editorFocusChanged(editor, activeFocus)

        Keys.onReturnPressed: event => { editor.state.commit("key"); event.accepted = true }
        Keys.onEnterPressed: event => { editor.state.commit("key"); event.accepted = true }
        Keys.onEscapePressed: event => { editor.state.cancel(); event.accepted = true }
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                editor.state.commit("key")
                event.accepted = true
            } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down
                       || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown) {
                // Keep the view from moving its selection under the editor.
                event.accepted = true
            }
        }
    }

    // A rejected name keeps the field open, focused, with the reason below.
    Connections {
        target: editor.state
        function onErrorChanged() {
            if (editor.editing && editor.state.error !== "") field.forceActiveFocus()
        }
    }
    Rectangle {
        objectName: "inlineNameError"
        visible: editor.editing && !!editor.state && editor.state.error !== ""
        anchors.top: parent.bottom
        anchors.topMargin: 2
        anchors.left: parent.left
        width: Math.max(parent.width, errorText.implicitWidth + 12)
        height: errorText.implicitHeight + 6
        radius: Theme.radiusSmall
        color: Theme.error
        Text {
            id: errorText
            anchors.centerIn: parent
            text: editor.state ? editor.state.error : ""
            color: Theme.base
            font.pointSize: Theme.fontSmall
        }
    }
}
