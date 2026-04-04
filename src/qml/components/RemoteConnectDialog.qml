import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import HyprFM
import Quill as Q

Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1000
    dialogWidth: 520
    title: "Connect to Server"
    subtitle: "Browse SFTP, SMB, and FTP locations through GVFS"
    initialFocusItem: hostField

    property string errorText: ""
    signal connected(string uri)

    function resetForm() {
        errorText = ""
        modeTabs.currentIndex = 0
        hostField.text = ""
        userField.text = ""
        portField.text = ""
        pathField.text = ""
        shareField.text = ""
        directUriField.text = ""
        addBookmarkToggle.checked = true
    }

    function currentUri() {
        if (modeTabs.currentIndex === 3)
            return directUriField.text.trim()

        var protocol = modeTabs.currentIndex === 0 ? "sftp" : modeTabs.currentIndex === 1 ? "smb" : "ftp"
        return remoteAccessService.buildUri(
            protocol,
            hostField.text,
            pathField.text,
            userField.text,
            parseInt(portField.text, 10),
            shareField.text
        )
    }

    function connectCurrentLocation() {
        errorText = ""
        var uri = currentUri()
        if (!uri) {
            errorText = modeTabs.currentIndex === 3
                ? "Enter a remote URI."
                : "Enter a host before connecting."
            return
        }
        remoteAccessService.connectToLocation(uri)
    }

    onOpened: Qt.callLater(function() { hostField.inputItem.forceActiveFocus() })

    Connections {
        target: remoteAccessService

        function onConnectionFinished(success, uri, error) {
            if (!root.visible)
                return

            if (!success) {
                root.errorText = error || "Couldn't connect to that location."
                return
            }

            if (addBookmarkToggle.checked)
                bookmarks.addBookmark(uri)
            root.connected(uri)
            root.accept()
        }
    }

    Q.Tabs {
        id: modeTabs
        Layout.fillWidth: true
        model: ["SFTP", "SMB", "FTP", "URI"]
    }

    // --- Mode controls ---

    Item {
        id: controlsContainer
        Layout.fillWidth: true
        clip: true
        property real contentHeight: modeTabs.currentIndex === 3
            ? uriControls.implicitHeight
            : modeTabs.currentIndex === 1
                ? smbControls.implicitHeight
                : standardControls.implicitHeight
        implicitHeight: contentHeight

        Behavior on contentHeight {
            NumberAnimation {
                duration: 220
                easing.type: Easing.OutCubic
            }
        }

        // SFTP / FTP controls
        ColumnLayout {
            id: standardControls
            anchors.left: parent.left
            anchors.right: parent.right
            visible: modeTabs.currentIndex === 0 || modeTabs.currentIndex === 2
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Host"
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                }

                Q.TextField {
                    id: hostField
                    Layout.fillWidth: true
                    variant: "filled"
                    placeholder: "example.com"
                    onTextChanged: root.errorText = ""
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Username"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: userField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "Optional"
                        onTextChanged: root.errorText = ""
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: 140
                    spacing: 4

                    Text {
                        text: "Port"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: portField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: modeTabs.currentIndex === 0 ? "22" : "21"
                        inputItem.validator: IntValidator { bottom: 1; top: 65535 }
                        onTextChanged: root.errorText = ""
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Remote folder"
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                }

                Q.TextField {
                    id: pathField
                    Layout.fillWidth: true
                    variant: "filled"
                    placeholder: "/home/user (optional)"
                    onTextChanged: root.errorText = ""
                }
            }
        }

        // SMB controls
        ColumnLayout {
            id: smbControls
            anchors.left: parent.left
            anchors.right: parent.right
            visible: modeTabs.currentIndex === 1
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 3
                    spacing: 4

                    Text {
                        text: "Host"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: smbHostField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "Server or NAS host"
                        onTextChanged: {
                            hostField.text = text
                            root.errorText = ""
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.maximumWidth: 120
                    spacing: 4

                    Text {
                        text: "Port"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: smbPortField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "445"
                        inputItem.validator: IntValidator { bottom: 1; top: 65535 }
                        onTextChanged: {
                            portField.text = text
                            root.errorText = ""
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Username"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: smbUserField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "Optional"
                        onTextChanged: {
                            userField.text = text
                            root.errorText = ""
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        text: "Share name"
                        color: Theme.subtext
                        font.pointSize: Theme.fontSmall
                    }

                    Q.TextField {
                        id: shareField
                        Layout.fillWidth: true
                        variant: "filled"
                        placeholder: "e.g. public, media"
                        onTextChanged: root.errorText = ""
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Folder inside share"
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                }

                Q.TextField {
                    id: smbPathField
                    Layout.fillWidth: true
                    variant: "filled"
                    placeholder: "Optional"
                    onTextChanged: {
                        pathField.text = text
                        root.errorText = ""
                    }
                }
            }
        }

        // URI controls
        ColumnLayout {
            id: uriControls
            anchors.left: parent.left
            anchors.right: parent.right
            visible: modeTabs.currentIndex === 3
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Text {
                    text: "Remote URI"
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                }

                Q.TextField {
                    id: directUriField
                    Layout.fillWidth: true
                    variant: "filled"
                    placeholder: "sftp://user@example.com/home/user"
                    onTextChanged: root.errorText = ""
                }
            }
        }
    }

    // --- Options ---

    Q.Toggle {
        id: addBookmarkToggle
        label: "Add to bookmarks after connecting"
        checked: true
    }

    // --- Separator ---

    Q.Separator { }

    // --- Preview ---

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: "Preview"
            color: Theme.text
            font.pointSize: Theme.fontNormal
            font.bold: true
        }
    }

    Q.Label {
        Layout.fillWidth: true
        text: currentUri() || "Complete the fields to generate the target URI."
        variant: "body"
        wrapMode: Text.WrapAnywhere
        color: currentUri() ? Theme.text : Theme.subtext
    }

    Q.Label {
        Layout.fillWidth: true
        text: modeTabs.currentIndex === 3
            ? "Use a full GVFS-compatible URI if you already have one."
            : "GVFS will prompt for credentials if the remote server requires them."
        variant: "caption"
        color: Theme.subtext
        wrapMode: Text.WordWrap
    }

    Q.Label {
        Layout.fillWidth: true
        text: runtimeFeatures.installHint("remoteAccess")
        variant: "caption"
        color: Theme.subtext
        wrapMode: Text.WordWrap
    }

    Q.Label {
        Layout.fillWidth: true
        visible: modeTabs.currentIndex === 1
        text: runtimeFeatures.installHint("smbRemoteAccess")
        variant: "caption"
        color: Theme.subtext
        wrapMode: Text.WordWrap
    }

    // --- Error message ---

    Text {
        Layout.fillWidth: true
        visible: errorText !== ""
        text: errorText
        color: Theme.error
        font.pointSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    // --- Action buttons ---

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12

        Q.Button {
            text: "Cancel"
            variant: "ghost"
            size: "small"
            enabled: !remoteAccessService.busy
            onClicked: root.reject()
        }

        Q.Button {
            text: remoteAccessService.busy ? "Connecting..." : "Connect"
            variant: "primary"
            size: "small"
            enabled: !remoteAccessService.busy
            onClicked: root.connectCurrentLocation()
        }
    }
}
