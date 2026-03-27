import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

    property var activeTab: null

    function startEditing() {
        breadcrumb.startEditing()
    }

    signal searchClicked()
    signal homeClicked()

    implicitHeight: toolbarColumn.implicitHeight
    color: Theme.base

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
                Rectangle {
                    width: 32; height: 32
                    radius: Theme.radiusSmall
                    color: backHover.containsMouse && root.activeTab && root.activeTab.canGoBack
                        ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent"
                    opacity: root.activeTab && root.activeTab.canGoBack ? 1.0 : 0.4
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                    IconChevronLeft { anchors.centerIn: parent; size: 18; color: Theme.text }
                    MouseArea {
                        id: backHover; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: root.activeTab ? root.activeTab.canGoBack : false
                        onClicked: { if (root.activeTab) root.activeTab.goBack() }
                    }
                }

                // Forward button
                Rectangle {
                    width: 32; height: 32
                    radius: Theme.radiusSmall
                    color: fwdHover.containsMouse && root.activeTab && root.activeTab.canGoForward
                        ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent"
                    opacity: root.activeTab && root.activeTab.canGoForward ? 1.0 : 0.4
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                    IconChevronRight { anchors.centerIn: parent; size: 18; color: Theme.text }
                    MouseArea {
                        id: fwdHover; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        enabled: root.activeTab ? root.activeTab.canGoForward : false
                        onClicked: { if (root.activeTab) root.activeTab.goForward() }
                    }
                }

                // Up button
                Rectangle {
                    width: 32; height: 32
                    radius: Theme.radiusSmall
                    color: upHover.containsMouse ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                    IconChevronUp { anchors.centerIn: parent; size: 18; color: Theme.text }
                    MouseArea {
                        id: upHover; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: { if (root.activeTab) root.activeTab.goUp() }
                    }
                }

                // Home button
                Rectangle {
                    width: 32; height: 32
                    radius: Theme.radiusSmall
                    color: homeHover.containsMouse ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                    IconHome { anchors.centerIn: parent; size: 18; color: Theme.text }
                    MouseArea {
                        id: homeHover; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.homeClicked()
                    }
                }

                // Breadcrumb / address bar
                Breadcrumb {
                    id: breadcrumb
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    path: root.activeTab ? root.activeTab.currentPath : ""
                    activeTab: root.activeTab
                    onNavigateRequested: (targetPath) => {
                        if (root.activeTab) root.activeTab.navigateTo(targetPath)
                    }
                }

                // Search button
                Rectangle {
                    width: 32; height: 32
                    radius: Theme.radiusSmall
                    color: searchHover.containsMouse ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.animDuration } }

                    IconSearch { anchors.centerIn: parent; size: 18; color: Theme.text }
                    MouseArea {
                        id: searchHover; anchors.fill: parent; hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.searchClicked()
                    }
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
                color: Theme.base

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
                    anchors.leftMargin: Theme.spacing
                    anchors.rightMargin: Theme.spacing
                    anchors.topMargin: 1
                    spacing: 2

                    // Track how many tabs are closing so others can grow immediately
                    property int closingCount: 0
                    property int effectiveCount: Math.max(tabModel.count - closingCount, 1)

                    Repeater {
                        id: tabRepeater
                        model: tabModel

                        delegate: Rectangle {
                            id: tabDelegate

                            required property int index
                            required property var model

                            Layout.fillHeight: true
                            Layout.preferredWidth: closing ? 0 : (tabRow.width - Theme.spacing * 2) / tabRow.effectiveCount
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

                            color: {
                                if (tabDelegate.index === tabModel.activeIndex)
                                    return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                                if (tabDelegateHover.containsMouse)
                                    return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
                                return "transparent"
                            }
                            radius: Theme.radiusSmall
                            border.width: tabDelegate.index === tabModel.activeIndex ? 1 : 0
                            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

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

                            MouseArea {
                                id: tabDelegateHover
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                cursorShape: Qt.PointingHandCursor
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.MiddleButton)
                                        tabDelegate.startClose()
                                    else
                                        tabModel.activeIndex = tabDelegate.index
                                }
                            }

                            // Tab label
                            Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.right: closeBtn.visible ? closeBtn.left : parent.right
                                anchors.rightMargin: closeBtn.visible ? 4 : 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: tabDelegate.model.title || "New Tab"
                                color: tabDelegate.index === tabModel.activeIndex ? Theme.text : Theme.subtext
                                font.pixelSize: Theme.fontNormal
                                font.weight: tabDelegate.index === tabModel.activeIndex ? Font.Medium : Font.Normal
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            // Close button — anchored to right edge so it moves with the tab
                            Rectangle {
                                id: closeBtn
                                width: 20; height: 20; radius: 10
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                visible: tabModel.count > 1
                                color: closeHover.hovered
                                    ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.8)
                                    : "transparent"

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
