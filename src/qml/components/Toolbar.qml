import QtQuick
import QtQuick.Layouts
import HyprFM

Rectangle {
    id: root

    property var activeTab: null

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

            Text {
                anchors.centerIn: parent
                text: "←"
                color: Theme.text
                font.pixelSize: Theme.fontLarge
            }

            MouseArea {
                id: backHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: root.activeTab && root.activeTab.canGoBack
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

            Text {
                anchors.centerIn: parent
                text: "→"
                color: Theme.text
                font.pixelSize: Theme.fontLarge
            }

            MouseArea {
                id: fwdHover
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                enabled: root.activeTab && root.activeTab.canGoForward
                onClicked: { if (root.activeTab) root.activeTab.goForward() }
            }
        }

        // Up button
        Rectangle {
            width: 32
            height: 32
            radius: Theme.radiusSmall
            color: upHover.containsMouse ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1) : "transparent"

            Text {
                anchors.centerIn: parent
                text: "↑"
                color: Theme.text
                font.pixelSize: Theme.fontLarge
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
                model: [
                    { mode: "grid",     icon: "▦" },
                    { mode: "list",     icon: "≡" },
                    { mode: "detailed", icon: "☰" }
                ]

                delegate: Rectangle {
                    width: 30
                    height: 30
                    radius: Theme.radiusSmall
                    color: {
                        const isActive = root.activeTab && root.activeTab.viewMode === modelData.mode
                        if (isActive) return Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.25)
                        if (vmHover.containsMouse) return Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                        return "transparent"
                    }

                    Text {
                        anchors.centerIn: parent
                        text: modelData.icon
                        color: root.activeTab && root.activeTab.viewMode === modelData.mode
                            ? Theme.accent
                            : Theme.subtext
                        font.pixelSize: Theme.fontNormal
                    }

                    MouseArea {
                        id: vmHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.activeTab) root.activeTab.setViewMode(modelData.mode)
                        }
                    }
                }
            }
        }

        // Ctrl+L shortcut to enter edit mode on breadcrumb
        Item {
            width: 0
            height: 0
            focus: false

            Shortcut {
                sequence: "Ctrl+L"
                onActivated: breadcrumb.startEditing()
            }
        }
    }
}
