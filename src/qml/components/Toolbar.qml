import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

    property var activeTab: null

    function startEditing() {
        breadcrumb.startEditing()
    }

    height: 44
    color: Theme.base

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        spacing: 4

        // Back button
        Rectangle {
            width: 32
            height: 32
            radius: Theme.radiusSmall
            color: backHover.containsMouse && root.activeTab && root.activeTab.canGoBack
                ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                : "transparent"
            opacity: root.activeTab && root.activeTab.canGoBack ? 1.0 : 0.4

            IconChevronLeft {
                anchors.centerIn: parent
                size: 18
                color: Theme.text
            }

            MouseArea {
                id: backHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: root.activeTab ? root.activeTab.canGoBack : false
                onClicked: { if (root.activeTab) root.activeTab.goBack() }
            }
        }

        // Forward button
        Rectangle {
            width: 32
            height: 32
            radius: Theme.radiusSmall
            color: fwdHover.containsMouse && root.activeTab && root.activeTab.canGoForward
                ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                : "transparent"
            opacity: root.activeTab && root.activeTab.canGoForward ? 1.0 : 0.4

            IconChevronRight {
                anchors.centerIn: parent
                size: 18
                color: Theme.text
            }

            MouseArea {
                id: fwdHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: root.activeTab ? root.activeTab.canGoForward : false
                onClicked: { if (root.activeTab) root.activeTab.goForward() }
            }
        }

        // Up button
        Rectangle {
            width: 32
            height: 32
            radius: Theme.radiusSmall
            color: upHover.containsMouse ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent"

            IconChevronUp {
                anchors.centerIn: parent
                size: 18
                color: Theme.text
            }

            MouseArea {
                id: upHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: { if (root.activeTab) root.activeTab.goUp() }
            }
        }

        // Breadcrumb
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

        // View mode toggle
        Row {
            spacing: 2

            Repeater {
                model: ["grid", "list", "detailed"]

                delegate: Rectangle {
                    width: 30
                    height: 30
                    radius: Theme.radiusSmall
                    property bool isActive: root.activeTab && root.activeTab.viewMode === modelData
                    color: {
                        if (isActive) return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.25)
                        if (vmHover.containsMouse) return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                        return "transparent"
                    }

                    property color iconColor: isActive ? Theme.accent : Theme.subtext

                    Loader {
                        anchors.centerIn: parent
                        sourceComponent: modelData === "grid" ? gridIcon
                                       : modelData === "list" ? listIcon
                                       : detailedIcon
                    }

                    Component {
                        id: gridIcon
                        IconGrid { size: 16; color: iconColor }
                    }
                    Component {
                        id: listIcon
                        IconList { size: 16; color: iconColor }
                    }
                    Component {
                        id: detailedIcon
                        IconAlignJustify { size: 16; color: iconColor }
                    }

                    MouseArea {
                        id: vmHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.activeTab) root.activeTab.viewMode = modelData
                        }
                    }
                }
            }
        }

        // Ctrl+L is handled by Main.qml's path_bar shortcut via toolbar.startEditing()
    }
}
