import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import HyprFM
import Quill as Q

// Asks for the passphrase of a locked LUKS or BitLocker drive when its Mount
// button is pressed (DeviceModel::unlockRequested).  Like the archive password
// prompt it stays up on a wrong passphrase, says so in place, and only closes
// once the drive is unlocked.
Q.Dialog {
    id: root
    anchors.fill: parent
    z: 1000
    dialogWidth: 430
    title: "Unlock encrypted drive"
    subtitle: root.deviceName
    initialFocusItem: passphraseField

    property string devicePath: ""
    property string deviceName: ""
    property string errorText: ""
    // A passphrase has been handed to udisks and we wait for its answer.
    property bool checking: false

    signal confirmed(string passphrase)

    function openFor(path, name) {
        root.devicePath = path
        root.deviceName = name || path
        root.errorText = ""
        root.checking = false
        passphraseField.text = ""
        root.open()
    }

    // Refused: stay open, say why, and select the attempt so typing replaces it.
    function failed(message) {
        root.checking = false
        root.errorText = message || "Wrong passphrase. Try again."
        passphraseField.inputItem.forceActiveFocus()
        passphraseField.inputItem.selectAll()
    }

    // Unlocked: nothing left to ask, and the passphrase does not linger.
    function succeeded() {
        root.checking = false
        passphraseField.text = ""
        root.accept()
    }

    function submit() {
        if (root.checking)
            return
        root.errorText = ""
        var pass = passphraseField.text
        if (pass === "") {
            root.errorText = "Enter the passphrase of the drive."
            return
        }
        root.checking = true
        root.confirmed(pass)
    }

    onOpened: Qt.callLater(function() { passphraseField.inputItem.forceActiveFocus() })
    onRejected: passphraseField.text = ""

    Q.TextField {
        id: passphraseField
        objectName: "unlockPassphraseField"
        Layout.fillWidth: true
        variant: "filled"
        placeholder: "Passphrase"
        echoMode: TextInput.Password
        enabled: !root.checking
        inputItem.Keys.onReturnPressed: root.submit()
        onTextChanged: root.errorText = ""
    }

    Text {
        Layout.fillWidth: true
        visible: root.errorText !== ""
        text: root.errorText
        color: Theme.error
        font.pointSize: Theme.fontSmall
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12

        Q.Button {
            objectName: "unlockDeviceCancelButton"
            text: "Cancel"
            variant: "ghost"
            size: "small"
            // Keep the same tint through the fade. Animating from transparent
            // black to a light surface briefly darkens the button.
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b,
                           activeFocus ? 0.18 : cancelHover.hovered ? 0.12 : 0)
            HoverHandler { id: cancelHover }
            onClicked: root.reject()
        }

        Q.Button {
            text: root.checking ? "Unlocking…" : "Unlock"
            variant: "primary"
            size: "small"
            enabled: !root.checking
            onClicked: root.submit()
        }
    }
}
