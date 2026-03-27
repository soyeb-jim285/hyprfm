import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

    property var activeTab: null
    property bool isRecentsView: false

    function startEditing() {
        breadcrumb.startEditing()
    }

    signal searchClicked()
    signal homeClicked()

    implicitHeight: toolbarColumn.implicitHeight
    color: Theme.mantle

    ColumnLayout {
        id: toolbarColumn
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        // ── Row 1: Navigation + Breadcrumb + Search ──
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 44

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing
                anchors.rightMargin: Theme.spacing
                spacing: 4

                // Back button
                HoverRect {
                    width: 32; height: 32
                    hoverEnabled: root.activeTab && root.activeTab.canGoBack
                    opacity: hoverEnabled ? 1.0 : 0.4
                    onClicked: { if (root.activeTab) root.activeTab.goBack() }
                    IconChevronLeft { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                // Forward button
                HoverRect {
                    width: 32; height: 32
                    hoverEnabled: root.activeTab && root.activeTab.canGoForward
                    opacity: hoverEnabled ? 1.0 : 0.4
                    onClicked: { if (root.activeTab) root.activeTab.goForward() }
                    IconChevronRight { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                // Up button
                HoverRect {
                    width: 32; height: 32
                    onClicked: { if (root.activeTab) root.activeTab.goUp() }
                    IconChevronUp { anchors.centerIn: parent; size: 18; color: Theme.text }
                }

                // Breadcrumb / address bar
                Breadcrumb {
                    id: breadcrumb
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    path: root.activeTab ? root.activeTab.currentPath : ""
                    activeTab: root.activeTab
                    isRecentsView: root.isRecentsView
                    onNavigateRequested: (targetPath) => {
                        if (root.activeTab) root.activeTab.navigateTo(targetPath)
                    }
                }

                // Search button
                HoverRect {
                    width: 32; height: 32
                    onClicked: root.searchClicked()
                    IconSearch { anchors.centerIn: parent; size: 18; color: Theme.text }
                }
            }
        }

        // ── Row 2: Tab bar (only visible with 2+ tabs) ──
        Item {
            id: tabBarRow
            Layout.fillWidth: true
            Layout.preferredHeight: tabModel.count > 1 ? 36 : 0
            visible: Layout.preferredHeight > 0 || tabBarHeightAnim.running
            clip: true

            Behavior on Layout.preferredHeight {
                NumberAnimation {
                    id: tabBarHeightAnim
                    duration: 250; easing.type: Easing.InOutCubic
                }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.mantle
                // Top separator
                Rectangle {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: 1
                    color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.06)
                }

                RowLayout {
                    id: tabRow
                    anchors.fill: parent
                    spacing: 0

                    // Track how many tabs are closing so others can grow immediately
                    property int closingCount: 0
                    property int effectiveCount: Math.max(tabModel.count - closingCount, 1)
                    property int hoveredIndex: -1

                    Repeater {
                        id: tabRepeater
                        model: tabModel

                        delegate: Rectangle {
                            id: tabDelegate

                            required property int index
                            required property var model

                            Layout.fillHeight: true
                            Layout.preferredWidth: closing ? 0 : tabRow.width / tabRow.effectiveCount
                            property bool closing: false

                            Behavior on Layout.preferredWidth {
                                NumberAnimation { duration: 220; easing.type: Easing.InOutCubic }
                            }

                            opacity: 0
                            scale: 0.94

                            property int frozenIndex: -1

                            function startClose() {
                                if (closing) return
                                frozenIndex = tabDelegate.index
                                closing = true
                                tabRow.closingCount++
                                exitAnim.start()
                            }

                            Component.onCompleted: enterAnim.start()

                            ParallelAnimation {
                                id: enterAnim
                                NumberAnimation {
                                    target: tabDelegate; property: "opacity"
                                    from: 0; to: 1; duration: 220
                                    easing.type: Easing.InOutCubic
                                }
                                NumberAnimation {
                                    target: tabDelegate; property: "scale"
                                    from: 0.88; to: 1; duration: 280
                                    easing.type: Easing.OutBack; easing.overshoot: 0.5
                                }
                            }

                            color: "transparent"

                            // Drop area on tab
                            DropArea {
                                id: tabDropArea
                                anchors.fill: parent
                                keys: ["text/uri-list"]

                                onDropped: (drop) => {
                                    var destPath = tabDelegate.model.path
                                    if (!destPath) return
                                    var urls = drop.urls
                                    var paths = []
                                    for (var i = 0; i < urls.length; i++) {
                                        var s = urls[i].toString()
                                        if (s.startsWith("file://"))
                                            paths.push(s.substring(7))
                                    }
                                    if (paths.length === 0) return
                                    if (drop.proposedAction === Qt.MoveAction)
                                        fileOps.moveFiles(paths, destPath)
                                    else
                                        fileOps.copyFiles(paths, destPath)
                                    drop.acceptProposedAction()
                                }
                            }

                            SequentialAnimation {
                                id: exitAnim
                                ParallelAnimation {
                                    NumberAnimation {
                                        target: tabDelegate; property: "opacity"
                                        to: 0; duration: 200; easing.type: Easing.InOutCubic
                                    }
                                    NumberAnimation {
                                        target: tabDelegate; property: "scale"
                                        to: 0.88; duration: 200; easing.type: Easing.InOutCubic
                                    }
                                }
                                ScriptAction {
                                    script: {
                                        tabRow.closingCount = Math.max(tabRow.closingCount - 1, 0)
                                        tabModel.closeTab(tabDelegate.frozenIndex)
                                    }
                                }
                            }

                            // Separator between tabs
                            Rectangle {
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                width: 1
                                height: parent.height * 0.5
                                color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
                                visible: tabDelegate.index < tabModel.count - 1
                                opacity: (tabDelegate.index === tabModel.activeIndex
                                    || tabDelegate.index + 1 === tabModel.activeIndex
                                    || tabDelegate.index === tabRow.hoveredIndex
                                    || tabDelegate.index + 1 === tabRow.hoveredIndex) ? 0 : 1
                                Behavior on opacity { NumberAnimation { duration: Theme.animDuration } }
                            }

                            HoverHandler {
                                id: tabDelegateHover
                                onHoveredChanged: {
                                    if (hovered) tabRow.hoveredIndex = tabDelegate.index
                                    else if (tabRow.hoveredIndex === tabDelegate.index) tabRow.hoveredIndex = -1
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.MiddleButton)
                                        tabDelegate.startClose()
                                    else
                                        tabModel.activeIndex = tabDelegate.index
                                }
                            }

                            // Inner highlight
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 5
                                radius: Theme.radiusSmall
                                color: {
                                    if (tabDelegate.index === tabModel.activeIndex)
                                        return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                                    if (tabDelegateHover.hovered)
                                        return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                                    return "transparent"
                                }
                                Behavior on color { ColorAnimation { duration: Theme.animDuration } }
                                border.width: tabDelegate.index === tabModel.activeIndex ? 1 : 0
                                border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                            }

                            // Tab label
                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                horizontalAlignment: Text.AlignHCenter
                                text: tabDelegate.model.title || "New Tab"
                                color: tabDelegate.index === tabModel.activeIndex ? Theme.text : Theme.subtext
                                font.pointSize: Theme.fontNormal
                                font.weight: tabDelegate.index === tabModel.activeIndex ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            // Close button — only visible on hover
                            Rectangle {
                                id: closeBtn
                                width: 20; height: 20; radius: 10
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                visible: tabModel.count > 1 && tabDelegateHover.hovered
                                color: closeHover.hovered
                                    ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.8)
                                    : "transparent"
                                Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                                IconX {
                                    anchors.centerIn: parent; size: 10
                                    color: closeHover.hovered ? Theme.base : Theme.muted
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: tabDelegate.startClose()
                                }

                                HoverHandler {
                                    id: closeHover
                                    cursorShape: Qt.PointingHandCursor
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
