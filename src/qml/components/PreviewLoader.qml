import QtQuick

// Shared async driver for the two preview panels: the quick-preview overlay
// and the Miller preview column. Both used to call PreviewService's blocking
// loaders straight from the GUI thread, so every selection change stalled
// rendering for as long as bat / pdfinfo / exiftool took to answer.
//
// The owner sets `path` and `kind` and calls reload(); results arrive on the
// preview properties. Font previews are deliberately NOT handled here:
// QFontDatabase is GUI-thread-only, and a font file is a local read rather
// than a subprocess, so the owner still loads those synchronously.
QtObject {
    id: loader

    // Identifies this panel to PreviewService. Staleness is tracked per
    // requester, so the two panels never cancel one another.
    property string requester: ""

    // Bound by the owner. Deliberately read at debounce-fire time rather
    // than inside reload(): both derive from the owner's fileProps, which is
    // assigned immediately before reload() is called, and QML has not yet
    // re-evaluated the dependent bindings at that point. Reading them one
    // event-loop turn later is what makes the dispatched kind match the file
    // actually selected -- otherwise a folder gets previewed as text and a
    // zip as a folder.
    property string path: ""

    // "text" | "pdf" | "archive" | "directory" | "" (metadata only)
    property string kind: ""

    // Only meaningful for encrypted archives. Set by the owner alongside
    // `path`, and read at dispatch time for the same reason.
    property string password: ""

    // True from dispatch until the matching result lands.
    property bool loading: false

    property var textPreview: ({ content: "", truncated: false, isBinary: false, error: "" })
    property var directoryPreview: ({ entries: [], truncated: false, error: "", count: 0 })
    property var pdfPreview: ({ localPath: "", pageCount: 0, error: "" })
    property var fileMetadata: ({})

    signal pdfPageCountChanged()

    function clear() {
        textPreview = ({ content: "", truncated: false, isBinary: false, error: "" })
        directoryPreview = ({ entries: [], truncated: false, error: "", count: 0 })
        pdfPreview = ({ localPath: "", pageCount: 0, error: "" })
        fileMetadata = ({})
    }

    // Drops any in-flight request and clears the panel.
    function stop() {
        _debounce.stop()
        previewService.cancelPreview(requester)
        loading = false
    }

    function reload() {
        _debounce.stop()
        clear()
        // Not `path === ""` here: path is still the previous file's at this
        // point (see above), so going from no selection straight to a file --
        // entering a folder auto-selects its first item -- skipped the load.
        // Dispatch even when there is no body to load: metadata alone costs
        // ~120 ms of exiftool/ffprobe and must not run on the GUI thread.
        loading = true
        _debounce.restart()
    }

    // Holding an arrow key walks the directory faster than any loader can
    // finish. Without this, every file passed over spawns processes whose
    // output nobody will ever see.
    property Timer _debounce: Timer {
        interval: 80
        onTriggered: {
            if (loader.path === "") {
                previewService.cancelPreview(loader.requester)
                loader.loading = false
                return
            }
            loader.loading = loader.kind !== ""
            previewService.requestPreview(loader.requester, loader.path, loader.kind,
                                          loader.password)
        }
    }

    property Connections _results: Connections {
        target: previewService
        function onPreviewReady(requester, path, data) {
            // PreviewService already drops superseded generations. The path
            // check covers the remaining case: the user left this file and
            // came back while the original request was still running.
            if (requester !== loader.requester || path !== loader.path)
                return

            loader.loading = false

            if (data.text !== undefined)
                loader.textPreview = data.text
            if (data.directory !== undefined)
                loader.directoryPreview = data.directory
            if (data.archive !== undefined)
                loader.directoryPreview = data.archive
            if (data.metadata !== undefined)
                loader.fileMetadata = data.metadata
            if (data.pdf !== undefined) {
                loader.pdfPreview = data.pdf
                loader.pdfPageCountChanged()
            }
        }
    }
}
