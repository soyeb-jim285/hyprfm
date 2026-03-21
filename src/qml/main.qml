import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 1024
    height: 768
    visible: true
    title: "HyprFM"
    color: "#1e1e2e"

    Text {
        anchors.centerIn: parent
        text: "HyprFM"
        color: "#cdd6f4"
        font.pixelSize: 24
    }
}
