import QtQuick
import QtQuick.Controls
import HyprFM

// Table preview for CSV/TSV: PreviewService's "table" (rows of cells, header
// first, plus a width in characters per column). A ListView only builds the
// rows on screen; the bat-highlighted text this replaces laid every row out
// as rich text up front, which took seconds for a big file.
ListView {
    id: root

    property var table: ({})
    property real fontSize: Theme.fontSmall

    readonly property var rows: table && table.rows ? table.rows : []
    readonly property int cellPadding: 8
    readonly property int rowHeight: Math.ceil(metrics.height) + 8
    // Average width per character, but never narrower than the bold header:
    // short headers of wide letters ("month") overrun the average, notably
    // with Segoe UI on Windows.
    readonly property var widths: (table && table.columnWidths ? table.columnWidths : [])
        .map(function(chars, i) {
            const header = rows.length > 0 && i < rows[0].length ? rows[0][i] : ""
            return Math.ceil(Math.max(chars * metrics.averageCharacterWidth,
                                      headerMetrics.advanceWidth(header))) + 2 * root.cellPadding
        })
    readonly property real tableWidth: widths.reduce(function(sum, w) { return sum + w }, 0)
    readonly property color lineColor: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.AutoFlickIfNeeded
    contentWidth: Math.max(width, tableWidth)
    model: Math.max(rows.length - 1, 0)
    headerPositioning: ListView.OverlayHeader

    FontMetrics {
        id: metrics
        font.pointSize: root.fontSize
    }

    FontMetrics {
        id: headerMetrics
        font.pointSize: root.fontSize
        font.bold: true
    }

    component TableRow: Row {
        id: tableRow
        property var cells: []
        property bool isHeader: false

        Repeater {
            model: root.widths.length

            Text {
                required property int index
                width: root.widths[index]
                height: root.rowHeight
                leftPadding: root.cellPadding
                rightPadding: root.cellPadding
                verticalAlignment: Text.AlignVCenter
                textFormat: Text.PlainText
                text: index < tableRow.cells.length ? tableRow.cells[index] : ""
                elide: Text.ElideRight
                color: Theme.text
                font.pointSize: root.fontSize
                font.bold: tableRow.isHeader

                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: root.lineColor
                }
            }
        }
    }

    header: Rectangle {
        z: 2
        width: root.contentWidth
        height: root.rowHeight
        color: Theme.surface

        TableRow {
            cells: root.rows.length > 0 ? root.rows[0] : []
            isHeader: true
        }
    }

    delegate: Rectangle {
        required property int index
        width: root.contentWidth
        height: root.rowHeight
        color: index % 2 ? root.lineColor : "transparent"

        TableRow {
            cells: root.rows[index + 1]
        }
    }

    footer: Text {
        visible: root.table && root.table.truncated === true
        height: visible ? implicitHeight + 16 : 0
        leftPadding: root.cellPadding
        topPadding: 8
        text: "Showing the first " + Math.max(root.rows.length - 1, 0) + " rows"
        color: Theme.subtext
        font.pointSize: root.fontSize
    }

    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
}
