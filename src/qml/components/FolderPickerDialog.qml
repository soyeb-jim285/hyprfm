import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HyprFM
import Quill as Q

// Mini file manager: browse a folder tree and confirm a directory.
// Used by Settings for choices like the startup_dir. Left click highlights a
// row and "Select this folder" picks it (or the folder being browsed if none
// is highlighted); double-click (or Enter on a highlighted row) descends into
// a subfolder. Clicking empty space, or right-clicking anywhere in the list,
// clears the highlight, like a folder picker on other platforms.
Q.Dialog {
    id: root
    anchors.fill: parent
    title: "Choose Folder"
    dialogWidth: 440
    z: 1100
    closeOnOverlayPress: false

    // Where the browser starts when opened. Empty = home.
    property string startingFolder: ""
    // The folder confirmed by the user (highlighted row, or the current dir).
    property string chosenFolder: ""
    property string currentDir: ""

    signal folderPicked(string path)

onOpened: {
        var start = root.startingFolder !== "" ? root.startingFolder : pickerFsModel.homePath()
        if (start.charAt(0) === "~")
            start = pickerFsModel.homePath() + start.slice(1)
        root.navigateTo(start)
        folderList.forceActiveFocus()
    }

    function navigateTo(path) {
        folderList.currentIndex = -1
        currentDir = path
        // The field starts bound to currentDir, but that binding is dropped on
        // the first manual edit, so every navigation must refresh it in sync.
        pathField.text = path
        pickerFsModel.setRootPath(path)
    }

    // True when the highlighted row is an actual folder (the list hides files).
    function highlightIsDir() {
        if (folderList.currentIndex < 0)
            return false
        return pickerFsModel.fileProperties(pickerFsModel.filePath(folderList.currentIndex)).isDir
    }

    // Type a path and press Enter to jump there: supports ~, absolute paths and
    // names relative to the folder currently shown. Invalid input keeps the old
    // directory.
    function goToPath(path) {
        var p = path.trim()
        if (p === "") {
            pathField.text = root.currentDir
            return
        }
        if (p.charAt(0) === "~")
            p = pickerFsModel.homePath() + p.slice(1)
        if (p.charAt(0) !== "/")
            p = root.currentDir + "/" + p
        var props = pickerFsModel.fileProperties(p)
        if (props.isDir) {
            root.navigateTo(props.path)
            folderList.forceActiveFocus()
        } else {
            pathField.text = root.currentDir
        }
    }

    function parentOf(path) {
        var p = path
        while (p.length > 1 && p.charAt(p.length - 1) === "/")
            p = p.slice(0, -1)
        var idx = p.lastIndexOf("/")
        return idx <= 0 ? "/" : p.slice(0, idx)
    }

    function goUp() {
        root.navigateTo(parentOf(root.currentDir))
    }

    function goHome() {
        root.navigateTo(pickerFsModel.homePath())
    }

    function enterFolder(path) {
        root.navigateTo(path)
    }

    function selectCurrent() {
        var path = root.currentDir
        if (root.highlightIsDir())
            path = pickerFsModel.filePath(folderList.currentIndex)
        root.chosenFolder = path
        root.folderPicked(path)
        root.accept()
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 4
        spacing: 8

        // "Up one level" arrow, like the back arrow of file explorers.
        Rectangle {
            Layout.preferredWidth: 34
            Layout.preferredHeight: 34
            Layout.alignment: Qt.AlignVCenter
            radius: Q.Theme.radius
            color: upMouse.containsMouse ? Q.Theme.surface1 : Q.Theme.surface0

            Text {
                anchors.centerIn: parent
                text: "\uf062"
                color: Theme.text
                font.family: Q.Theme.iconFont
                font.pixelSize: 14
            }

            MouseArea {
                id: upMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.goUp()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 34

            Q.TextField {
                id: pathField
                anchors.fill: parent
                text: root.currentDir
                variant: "filled"
                placeholder: "Type a path and press Enter"
                onHasActiveFocusChanged: {
                    if (pathField.hasActiveFocus)
                        pathField.inputItem.selectAll()
                }
                onSubmitted: (text) => root.goToPath(text)
            }

            // A long path is shown as ".../tail" while the field is idle, so it
            // never spills out of the box. Focusing the field reveals the full
            // editable text.
            Text {
                visible: !pathField.hasActiveFocus
                         && pathField.inputItem.contentWidth > pathField.inputItem.width
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing
                anchors.rightMargin: Theme.spacing
                text: pathField.text
                color: Theme.subtext
                font.pixelSize: Q.Theme.fontSize
                font.family: Q.Theme.fontFamily
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideLeft
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 300
        color: "transparent"
        clip: true

        // Any click landing outside a row (no highlighted folder) clears the
        // selection, so "Select this folder" keeps meaning the folder shown
        // in the path field instead of silently guessing.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: folderList.currentIndex = -1
        }

        ListView {
            id: folderList
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Math.min(300, contentHeight)
            clip: true
            model: pickerFsModel
            focus: true
            keyNavigationWraps: true

            Keys.onReturnPressed: enterHighlighted()
            Keys.onEnterPressed: enterHighlighted()
            function enterHighlighted() {
                if (root.highlightIsDir())
                    root.enterFolder(pickerFsModel.filePath(folderList.currentIndex))
            }

            delegate: Rectangle {
                required property int index
                required property bool isDir
                required property string fileName
                required property string fileIconName
                required property bool isSymlink

                // Files are ignored: the picker only browses into folders.
                // A hidden row must take no height at all, otherwise it eats
                // clicks meant for the empty area behind the list.
                visible: isDir
                height: isDir ? 34 : 0
                width: folderList.width
                radius: Theme.radiusSmall
                color: mouse.containsMouse
                    ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.08)
                    : (ListView.isCurrentItem
                       ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                       : "transparent")

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Image {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        Layout.alignment: Qt.AlignVCenter
                        source: "image://icon/" + fileIconName + "?theme=" + config.iconTheme
                        sourceSize: Qt.size(18, 18)
                    }

                    Text {
                        text: fileName + (isSymlink ? " (link)" : "")
                        color: Theme.text
                        font.pointSize: Theme.fontNormal
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: (event) => {
                        if (event.button === Qt.RightButton) {
                            folderList.currentIndex = -1
                        } else {
                            folderList.currentIndex = index
                            folderList.forceActiveFocus()
                        }
                    }
                    onDoubleClicked: (event) => {
                        if (event.button !== Qt.LeftButton)
                            return
                        folderList.currentIndex = index
                        root.enterFolder(pickerFsModel.filePath(index))
                    }
                }
            }

            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: 10
        spacing: 8

        Q.Button {
            text: "Home"
            variant: "ghost"
            onClicked: root.goHome()
        }

        Item { Layout.fillWidth: true }

        Q.Button {
            text: "Cancel"
            variant: "ghost"
            onClicked: root.reject()
        }
        Q.Button {
            text: "Select this folder"
            variant: "primary"
            onClicked: root.selectCurrent()
        }
    }
}