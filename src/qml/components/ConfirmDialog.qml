import QtQuick
import QtQuick.Layouts
import HyprFM
import Quill as Q

// A yes/no dialog: a line of prose and two buttons. Callers set `message`,
// the confirm button's `confirmText`/`confirmVariant`, and act on accepted().
Q.Dialog {
    id: root
    anchors.fill: parent
    z: 9998
    dialogWidth: 360
    initialFocusItem: cancelButton

    property string message: ""
    property string confirmText: "OK"
    property string confirmVariant: "danger"

    Text {
        Layout.fillWidth: true
        textFormat: Text.PlainText
        text: root.message
        color: Theme.subtext
        font.pointSize: Theme.fontNormal
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.alignment: Qt.AlignRight
        spacing: 12

        Q.Button {
            id: cancelButton
            text: "Cancel"
            variant: "ghost"
            size: "small"
            KeyNavigation.left: confirmButton
            KeyNavigation.right: confirmButton
            KeyNavigation.tab: confirmButton
            KeyNavigation.backtab: confirmButton
            Keys.onLeftPressed: confirmButton.forceActiveFocus()
            Keys.onRightPressed: confirmButton.forceActiveFocus()
            onClicked: root.reject()
        }

        Q.Button {
            id: confirmButton
            text: root.confirmText
            variant: root.confirmVariant
            size: "small"
            KeyNavigation.left: cancelButton
            KeyNavigation.right: cancelButton
            KeyNavigation.tab: cancelButton
            KeyNavigation.backtab: cancelButton
            Keys.onLeftPressed: cancelButton.forceActiveFocus()
            Keys.onRightPressed: cancelButton.forceActiveFocus()
            onClicked: root.accept()
        }
    }
}
