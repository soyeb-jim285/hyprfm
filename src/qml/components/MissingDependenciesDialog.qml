import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1002
    dialogWidth: Math.min(720, width - 40)
    title: "Missing dependencies"
    subtitle: "HyprFM works best when the following tools and features are available."

    // Three model arrays, fed from `dependencies.missingDependencies` on open().
    property var missingRequired: []
    property var missingOptional: []
    property var missingFeatures: []

    function openDialog() {
        refreshLists()
        open()
    }

    function refreshLists() {
        if (!dependencies) {
            missingRequired = []
            missingOptional = []
            missingFeatures = []
            return
        }
        dependencies.refresh()
        var list = dependencies.missingDependencies || []
        var req = [], opt = [], feat = []
        for (var i = 0; i < list.length; ++i) {
            var d = list[i]
            if (d.kind === "feature")
                feat.push(d)
            else if (d.required)
                req.push(d)
            else
                opt.push(d)
        }
        missingRequired = req
        missingOptional = opt
        missingFeatures = feat
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 16

        // Distro banner
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: distroRow.implicitHeight + 20
            radius: Theme.radiusSmall
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
            border.width: 1
            border.color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.25)

            RowLayout {
                id: distroRow
                anchors.fill: parent
                anchors.margins: 10
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: dependencies
                        ? "Detected distribution: <b>" + dependencies.distroName + "</b>"
                        : "Detected distribution: unknown"
                    color: Theme.text
                    font.pointSize: Theme.fontNormal
                    textFormat: Text.RichText
                    wrapMode: Text.WordWrap
                }
            }
        }

        // Scrollable section container
        Item {
            Layout.fillWidth: true
            implicitHeight: Math.min(520, depsFlick.contentHeight)

            Flickable {
                id: depsFlick
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: depsColumn.implicitHeight
                boundsBehavior: Flickable.StopAtBounds

                ColumnLayout {
                    id: depsColumn
                    width: depsFlick.width
                    spacing: 20

                    // ── Required tools ────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: root.missingRequired.length > 0

                        Text {
                            text: "Required"
                            color: Theme.text
                            font.pointSize: Theme.fontNormal
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "These are needed for core file operations. "
                                + "HyprFM may not work correctly until they are installed."
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: root.missingRequired
                            delegate: DependencyRow {
                                Layout.fillWidth: true
                                dep: modelData
                                severityColor: Qt.rgba(0.9, 0.3, 0.3, 1.0)
                            }
                        }
                    }

                    // ── Optional tools / services ─────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: root.missingOptional.length > 0

                        Text {
                            text: "Optional"
                            color: Theme.text
                            font.pointSize: Theme.fontNormal
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "These enable extra features. HyprFM works without them but certain actions will be unavailable."
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: root.missingOptional
                            delegate: DependencyRow {
                                Layout.fillWidth: true
                                dep: modelData
                                severityColor: Theme.accent
                            }
                        }
                    }

                    // ── Compile-time features ─────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: root.missingFeatures.length > 0

                        Text {
                            text: "Build-time features"
                            color: Theme.text
                            font.pointSize: Theme.fontNormal
                            font.weight: Font.DemiBold
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "These are compiled into HyprFM at build time. To enable them, install the full (non-minimal) package or rebuild from source with the listed libraries available."
                            color: Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }

                        Repeater {
                            model: root.missingFeatures
                            delegate: DependencyRow {
                                Layout.fillWidth: true
                                dep: modelData
                                severityColor: Qt.rgba(Theme.subtext.r, Theme.subtext.g, Theme.subtext.b, 1.0)
                            }
                        }
                    }

                    // All good state
                    Text {
                        Layout.fillWidth: true
                        visible: root.missingRequired.length === 0
                              && root.missingOptional.length === 0
                              && root.missingFeatures.length === 0
                        text: "All dependencies are installed. You're good to go."
                        color: Theme.subtext
                        font.pointSize: Theme.fontNormal
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        // Footer
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: "Click 'Copy' to copy the install command, run it in a terminal, then click 'Re-check'."
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            Q.Button {
                text: "Re-check"
                onClicked: root.refreshLists()
            }

            Q.Button {
                text: "Close"
                onClicked: root.close()
            }
        }
    }

    // Inline delegate component defined via a separate file would be cleaner,
    // but keeping it inline here matches the single-file pattern used by the
    // other dialogs in this directory.
    component DependencyRow: Rectangle {
        id: depRow
        property var dep
        property color severityColor: Theme.accent

        implicitHeight: rowCol.implicitHeight + 20
        radius: Theme.radiusSmall
        color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.04)
        border.width: 1
        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

        ColumnLayout {
            id: rowCol
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: depRow.severityColor
                    Layout.alignment: Qt.AlignVCenter
                }

                Text {
                    Layout.fillWidth: true
                    text: depRow.dep ? depRow.dep.displayName : ""
                    color: Theme.text
                    font.pointSize: Theme.fontNormal
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
            }

            Text {
                Layout.fillWidth: true
                text: depRow.dep ? depRow.dep.purpose : ""
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: cmdText.implicitHeight + 14
                    radius: Theme.radiusSmall
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                    border.width: 1
                    border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)

                    TextEdit {
                        id: cmdText
                        anchors.fill: parent
                        anchors.margins: 7
                        text: depRow.dep ? depRow.dep.installCommand : ""
                        color: Theme.text
                        font.family: "monospace"
                        font.pointSize: Theme.fontSmall
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                    }
                }

                Q.Button {
                    text: "Copy"
                    visible: depRow.dep && depRow.dep.installCommand.length > 0
                    onClicked: {
                        clipboard.copyText(depRow.dep.installCommand)
                        toast.show("Copied to clipboard", "success")
                    }
                }
            }
        }
    }
}
