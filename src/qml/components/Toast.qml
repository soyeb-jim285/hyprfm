import QtQuick
import HyprFM

// Toast notification container – anchored bottom-right of its parent.
// Usage: toast.show("message", "info"|"error"|"success")
Item {
    id: root
    Accessible.role: Accessible.AlertMessage
    Accessible.name: "Notifications"

    // Size to the column of toasts
    implicitWidth: toastColumn.implicitWidth
    implicitHeight: toastColumn.implicitHeight

    z: 200

    ListModel {
        id: toastModel
    }

    Column {
        id: toastColumn
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 8

        Repeater {
            model: toastModel

            delegate: Item {
                id: toastItem
                implicitWidth: toastRect.implicitWidth
                implicitHeight: toastRect.implicitHeight
                opacity: 1.0

                required property string message
                required property string toastType
                required property int toastIndex

                Rectangle {
                    id: toastRect
                    implicitWidth: Math.max(240, toastText.implicitWidth + 32)
                    implicitHeight: toastText.implicitHeight + 24
                    radius: Theme.radiusMedium
                    color: Theme.mantle

                    border.width: 2
                    border.color: toastItem.toastType === "error"   ? Theme.error
                                : toastItem.toastType === "success" ? Theme.success
                                : Theme.accent

                    Text {
                        id: toastText
                        anchors.centerIn: parent
                        text: toastItem.message
                        color: Theme.text
                        font.pointSize: Theme.fontNormal
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        width: Math.min(implicitWidth, 340)
                    }
                }

                Timer {
                    id: dismissTimer
                    interval: 3000
                    running: true
                    onTriggered: fadeOut.start()
                }

                SequentialAnimation {
                    id: fadeOut
                    NumberAnimation {
                        target: toastItem
                        property: "opacity"
                        to: 0
                        duration: 300
                        easing.type: Easing.InQuad
                    }
                    ScriptAction {
                        script: {
                            // Find and remove by toastIndex
                            for (var i = 0; i < toastModel.count; i++) {
                                if (toastModel.get(i).toastIndex === toastItem.toastIndex) {
                                    toastModel.remove(i)
                                    break
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Counter to give each toast a unique id
    property int _nextIndex: 0

    function show(message, type) {
        toastModel.append({
            message:    message,
            toastType:  type || "info",
            toastIndex: _nextIndex
        })
        _nextIndex++
    }
}
