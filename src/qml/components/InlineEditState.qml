import QtQuick

// Naming a file or folder in place, inside a file view, instead of in a
// dialog. Each view owns one of these; its delegates show an
// InlineNameEditor over their name while `path` is theirs.
//
// Main.qml starts it with begin(path, text, commit, cancel, waitForRow):
// commit(path, name, keepSelection) does the rename and returns "" or an
// error to show, keeping the field open. keepSelection is set when the edit
// ended because the user clicked or scrolled elsewhere, so the renamed entry
// must not take the selection back. cancel(path) runs on Escape. waitForRow
// lets a just-created entry appear in the model first.
//
// A view is a focus scope that hands focus back to the field inside it, so
// clicks in the view are caught through its interactionStarted signal rather
// than as focus loss; focus loss covers leaving the view altogether.
QtObject {
    id: state

    property Item view: null
    // The entry being edited; "" when nothing is.
    property string path: ""
    property string text: ""
    property string error: ""
    readonly property bool active: path !== ""
    // Waiting for a just-created entry to show up in the view.
    readonly property bool pending: pendingPath !== ""

    property string pendingPath: ""
    property string pendingText: ""
    property var commitHandler: null
    property var cancelHandler: null
    // The editor currently holding keyboard focus, if any. Delegates are
    // recycled and recreated (scrolling, model refreshes), so focus moving
    // from one editor instance to another must not count as leaving.
    property Item focusedEditor: null
    property int retries: 0

    function begin(targetPath, initialText, onCommit, onCancel, waitForRow) {
        if (active) commit()
        if (!view || !targetPath) return false
        pendingPath = targetPath
        pendingText = initialText
        commitHandler = onCommit || null
        cancelHandler = onCancel || null
        if (activatePending()) return true
        if (!waitForRow) { clear(); return false }
        retries = 0
        rowTimer.start()
        return true
    }

    function activatePending() {
        if (!pending || !view || view.rowForPath(pendingPath) < 0) return false
        view.focusPath(pendingPath, true)
        error = ""
        text = pendingText
        path = pendingPath
        pendingPath = ""
        return true
    }

    // reason: "key" (Enter/Tab), "elsewhere" (a click or scroll in the view),
    // "blur" (focus left the view). Only "key" moves the selection to the
    // renamed entry, and "blur" leaves keyboard focus where it went.
    function commit(reason) {
        if (!active) return true
        reason = reason || "key"
        const original = path
        const name = text.trim()
        if (name === "") { cancel(); return true }
        const handler = commitHandler
        const failure = handler ? handler(original, name, reason !== "key") : ""
        if (failure) {
            if (path === original) error = failure
            return false
        }
        if (path === original) clear()
        if (reason !== "blur" && view) view.forceActiveFocus()
        return true
    }

    function cancel() {
        const original = path
        const handler = cancelHandler
        clear()
        if (handler) handler(original)
        else if (view && original) view.focusPath(original, false)
    }

    function clear() {
        rowTimer.stop()
        blurTimer.stop()
        path = ""
        pendingPath = ""
        text = ""
        error = ""
        commitHandler = null
        cancelHandler = null
        focusedEditor = null
    }

    function editorFocusChanged(editor, hasFocus) {
        if (hasFocus) {
            focusedEditor = editor
            blurTimer.stop()
        } else if (focusedEditor === editor) {
            focusedEditor = null
            blurTimer.restart()
        }
    }

    property Connections viewInteraction: Connections {
        target: state.view
        ignoreUnknownSignals: true
        function onInteractionStarted() {
            if (state.active) state.commit("elsewhere")
        }
    }

    property Timer rowTimer: Timer {
        interval: 50
        repeat: true
        onTriggered: {
            if (state.activatePending() || ++state.retries > 80) {
                stop()
                if (state.pending) state.clear()
            }
        }
    }

    // Focus went elsewhere: apply the name, unless another editor for the
    // same entry took over or the whole window was left (Alt+Tab keeps the
    // edit open, as text fields do).
    property Timer blurTimer: Timer {
        interval: 150
        onTriggered: {
            const window = state.view ? state.view.Window.window : null
            if (state.active && !state.focusedEditor && window && window.active)
                state.commit("blur")
        }
    }
}
