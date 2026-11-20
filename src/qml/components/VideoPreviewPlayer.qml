import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import HyprFM

Item {
    id: root

    property string filePath: ""
    property bool active: true
    property bool autoPlay: true
    property real volume: 0.8
    property bool muted: false
    property bool loop: false
    property bool controlsVisible: true
    property bool isSeeking: false

    readonly property bool isPlaying: player.playbackState === MediaPlayer.PlayingState
    readonly property bool isPaused: player.playbackState === MediaPlayer.PausedState
    readonly property int duration: player.duration
    readonly property int position: player.position
    readonly property real aspectRatio: (videoOut.contentRect.height > 0)
        ? (videoOut.contentRect.width / videoOut.contentRect.height)
        : 0

    function play() {
        player.play()
    }

    function pause() {
        player.pause()
    }

    function togglePlayPause() {
        if (isPlaying) {
            pause()
        } else {
            play()
        }
    }

    function stop() {
        player.stop()
    }

    function seek(posMs) {
        if (player.seekable) {
            player.position = Math.max(0, Math.min(player.duration, Math.round(posMs)))
        }
    }

    function seekRelative(deltaMs) {
        seek(player.position + deltaMs)
        showControlsBriefly()
    }

    function setVolume(val) {
        root.volume = Math.max(0.0, Math.min(1.0, val))
        if (root.muted && val > 0)
            root.muted = false
        showControlsBriefly()
    }

    function toggleMute() {
        root.muted = !root.muted
        showControlsBriefly()
    }

    function toggleLoop() {
        root.loop = !root.loop
        showControlsBriefly()
    }

    function showControlsBriefly() {
        root.controlsVisible = true
        if (root.isPlaying)
            hideTimer.restart()
    }

    function formatTime(ms) {
        if (isNaN(ms) || ms <= 0)
            return "00:00"
        var totalSec = Math.floor(ms / 1000)
        var hours = Math.floor(totalSec / 3600)
        var minutes = Math.floor((totalSec % 3600) / 60)
        var seconds = totalSec % 60
        var mStr = (minutes < 10 ? "0" : "") + minutes
        var sStr = (seconds < 10 ? "0" : "") + seconds
        if (hours > 0) {
            var hStr = (hours < 10 ? "0" : "") + hours
            return hStr + ":" + mStr + ":" + sStr
        }
        return mStr + ":" + sStr
    }

    function toFileUrl(path) {
        if (!path || path === "")
            return ""
        if (path.indexOf("://") >= 0)
            return path
        var parts = path.split("/")
        var encoded = []
        for (var i = 0; i < parts.length; i++) {
            encoded.push(encodeURIComponent(parts[i]))
        }
        return "file://" + encoded.join("/")
    }

    readonly property url mediaSource: (root.active && root.filePath !== "")
        ? root.toFileUrl(root.filePath)
        : ""

    onMediaSourceChanged: {
        if (mediaSource != "" && active && autoPlay) {
            player.play()
        } else if (mediaSource == "") {
            player.stop()
        }
    }

    onActiveChanged: {
        if (!active) {
            player.stop()
        } else if (mediaSource != "" && autoPlay) {
            player.play()
        }
    }

    Timer {
        id: hideTimer
        interval: 2500
        repeat: false
        onTriggered: {
            if (root.isPlaying && !controlBarMouse.containsMouse && !scrubberMouse.pressed && !volSliderMouse.pressed) {
                root.controlsVisible = false
            }
        }
    }

    // Video Player core
    MediaPlayer {
        id: player
        source: root.mediaSource
        audioOutput: AudioOutput {
            id: audioOut
            volume: root.muted ? 0.0 : root.volume
            muted: root.muted
        }
        videoOutput: videoOut
        loops: root.loop ? MediaPlayer.Infinite : 1

        onPlaybackStateChanged: {
            if (playbackState === MediaPlayer.PlayingState) {
                hideTimer.restart()
            } else {
                root.controlsVisible = true
                hideTimer.stop()
            }
        }

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia && root.autoPlay && root.active) {
                player.play()
            }
        }
    }

    // Canvas background
    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: Theme.radiusMedium
        clip: true

        VideoOutput {
            id: videoOut
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
        }

        // Mouse interaction for click-to-play/pause and hover tracking
        MouseArea {
            id: videoCanvasMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: (!root.controlsVisible && root.isPlaying) ? Qt.BlankCursor : Qt.PointingHandCursor

            onPositionChanged: {
                root.controlsVisible = true
                if (root.isPlaying)
                    hideTimer.restart()
            }

            onClicked: {
                root.togglePlayPause()
            }

            onWheel: (wheel) => {
                var delta = wheel.angleDelta.y > 0 ? 0.05 : -0.05
                root.setVolume(root.volume + delta)
                wheel.accepted = true
            }
        }

        // Center Splash Play Button (visible when paused)
        Rectangle {
            id: centerPlayBtn
            x: videoOut.contentRect.width > 0 ? (videoOut.contentRect.x + (videoOut.contentRect.width - width) / 2) : (parent.width - width) / 2
            y: videoOut.contentRect.height > 0 ? (videoOut.contentRect.y + (videoOut.contentRect.height - height) / 2) : (parent.height - height) / 2
            width: 72
            height: 72
            radius: 36
            color: Qt.rgba(Theme.mantle.r, Theme.mantle.g, Theme.mantle.b, 0.82)
            border.width: 1.5
            border.color: centerMouse.containsMouse
                ? Theme.accent
                : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.22)
            visible: !root.isPlaying
            opacity: root.controlsVisible ? 1.0 : 0.0
            scale: centerMouse.containsMouse ? 1.08 : 1.0

            Behavior on scale { NumberAnimation { duration: Theme.animDurationFast } }
            Behavior on opacity { NumberAnimation { duration: Theme.animDurationFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.animDurationFast } }

            IconPlay {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: 3
                size: 28
                color: centerMouse.containsMouse ? Theme.accent : Theme.text
            }

            MouseArea {
                id: centerMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.play()
            }
        }

        // Floating Control Bar
        Rectangle {
            id: controlBar
            anchors.horizontalCenter: parent.horizontalCenter
            y: (videoOut.contentRect.height > 0 && videoOut.contentRect.y >= 0)
                ? Math.max(8, Math.min(parent.height - height - 8, videoOut.contentRect.y + videoOut.contentRect.height - height - 12))
                : (parent.height - height - 14)
            width: Math.min(parent.width - 24, Math.max(320, Math.min(620, (videoOut.contentRect.width > 0 ? videoOut.contentRect.width - 24 : 620))))
            height: 52
            radius: 26
            color: Qt.rgba(Theme.mantle.r, Theme.mantle.g, Theme.mantle.b, 0.88)
            border.width: 1
            border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.15)
            opacity: root.controlsVisible ? 1.0 : 0.0
            visible: opacity > 0

            Behavior on opacity { NumberAnimation { duration: 180 } }

            MouseArea {
                id: controlBarMouse
                anchors.fill: parent
                hoverEnabled: true
                onEntered: {
                    hideTimer.stop()
                    root.controlsVisible = true
                }
                onExited: {
                    if (root.isPlaying)
                        hideTimer.restart()
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 14
                spacing: 8

                // Play / Pause Button
                Rectangle {
                    width: 34
                    height: 34
                    radius: 17
                    color: playBtnMouse.containsMouse
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                        : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)

                    Behavior on color { ColorAnimation { duration: 120 } }

                    Item {
                        anchors.centerIn: parent
                        width: 18
                        height: 18

                        IconPlay {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 1.5
                            size: 18
                            color: Theme.accent
                            visible: !root.isPlaying
                        }

                        IconPause {
                            anchors.centerIn: parent
                            size: 18
                            color: Theme.accent
                            visible: root.isPlaying
                        }
                    }

                    MouseArea {
                        id: playBtnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.togglePlayPause()
                    }
                }

                // Skip -5s
                Rectangle {
                    width: 28
                    height: 28
                    radius: 14
                    color: skipBackMouse.containsMouse
                        ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
                        : "transparent"

                    IconSkipBack {
                        anchors.centerIn: parent
                        size: 14
                        color: skipBackMouse.containsMouse ? Theme.text : Theme.subtext
                    }

                    MouseArea {
                        id: skipBackMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.seekRelative(-5000)
                    }
                }

                // Current Time Text
                Text {
                    text: root.formatTime(player.position)
                    color: Theme.text
                    font.pointSize: Theme.fontSmall
                    font.weight: Font.DemiBold
                }

                // Interactive Scrubber / Seekbar
                Item {
                    id: scrubber
                    Layout.fillWidth: true
                    height: 24

                    property real ratio: player.duration > 0
                        ? Math.max(0, Math.min(1, player.position / player.duration))
                        : 0

                    // Hover hint tooltip
                    Rectangle {
                        id: seekHint
                        visible: scrubberMouse.containsMouse && player.duration > 0
                        x: Math.max(0, Math.min(scrubber.width - width, scrubberMouse.mouseX - width / 2))
                        y: -24
                        width: hintText.implicitWidth + 10
                        height: 18
                        radius: 4
                        color: Qt.rgba(Theme.crust.r, Theme.crust.g, Theme.crust.b, 0.95)
                        border.width: 1
                        border.color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.2)

                        Text {
                            id: hintText
                            anchors.centerIn: parent
                            text: {
                                var r = Math.max(0, Math.min(1, scrubberMouse.mouseX / scrubber.width))
                                return root.formatTime(r * player.duration)
                            }
                            color: Theme.text
                            font.pointSize: Theme.fontSmall - 1
                        }
                    }

                    // Background track
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: (scrubberMouse.containsMouse || scrubberMouse.pressed) ? 6 : 4
                        radius: 3
                        color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.18)

                        Behavior on height { NumberAnimation { duration: 100 } }
                    }

                    // Played progress bar
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * scrubber.ratio
                        height: (scrubberMouse.containsMouse || scrubberMouse.pressed) ? 6 : 4
                        radius: 3
                        color: Theme.accent

                        Behavior on height { NumberAnimation { duration: 100 } }
                    }

                    // Handle Knob
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        x: Math.max(0, Math.min(scrubber.width - width, (scrubber.width * scrubber.ratio) - width / 2))
                        width: (scrubberMouse.containsMouse || scrubberMouse.pressed) ? 14 : 10
                        height: width
                        radius: width / 2
                        color: scrubberMouse.pressed ? Qt.lighter(Theme.accent, 1.2) : Theme.accent
                        border.width: 1.5
                        border.color: Qt.rgba(Theme.mantle.r, Theme.mantle.g, Theme.mantle.b, 0.9)

                        Behavior on width { NumberAnimation { duration: 100 } }
                    }

                    MouseArea {
                        id: scrubberMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor

                        function updateSeek(mouseEvent) {
                            if (player.duration > 0) {
                                var r = Math.max(0, Math.min(1, mouseEvent.x / width))
                                root.seek(r * player.duration)
                            }
                        }

                        onPressed: (mouse) => {
                            root.isSeeking = true
                            hideTimer.stop()
                            updateSeek(mouse)
                        }

                        onPositionChanged: (mouse) => {
                            if (pressed) {
                                updateSeek(mouse)
                            }
                        }

                        onReleased: {
                            root.isSeeking = false
                            if (root.isPlaying)
                                hideTimer.restart()
                        }
                    }
                }

                // Total Duration Text
                Text {
                    text: root.formatTime(player.duration)
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                }

                // Skip +5s
                Rectangle {
                    width: 28
                    height: 28
                    radius: 14
                    color: skipFwdMouse.containsMouse
                        ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
                        : "transparent"

                    IconSkipForward {
                        anchors.centerIn: parent
                        size: 14
                        color: skipFwdMouse.containsMouse ? Theme.text : Theme.subtext
                    }

                    MouseArea {
                        id: skipFwdMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.seekRelative(5000)
                    }
                }

                // Volume button + mini slider
                RowLayout {
                    spacing: 4

                    Rectangle {
                        width: 28
                        height: 28
                        radius: 14
                        color: volBtnMouse.containsMouse
                            ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
                            : "transparent"

                        Item {
                            anchors.centerIn: parent
                            width: 16
                            height: 16

                            IconVolumeX {
                                anchors.centerIn: parent
                                size: 16
                                color: Theme.accent
                                visible: root.muted || root.volume <= 0.01
                            }

                            IconVolume1 {
                                anchors.centerIn: parent
                                size: 16
                                color: Theme.text
                                visible: !root.muted && root.volume > 0.01 && root.volume < 0.5
                            }

                            IconVolume2 {
                                anchors.centerIn: parent
                                size: 16
                                color: Theme.text
                                visible: !root.muted && root.volume >= 0.5
                            }
                        }

                        MouseArea {
                            id: volBtnMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.toggleMute()
                        }
                    }

                    // Mini volume track
                    Item {
                        id: volSlider
                        width: 54
                        height: 24

                        property real ratio: root.muted ? 0.0 : root.volume

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.18)
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width * volSlider.ratio
                            height: 4
                            radius: 2
                            color: Theme.accent
                        }

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            x: Math.max(0, Math.min(volSlider.width - width, (volSlider.width * volSlider.ratio) - width / 2))
                            width: 10
                            height: 10
                            radius: 5
                            color: volSliderMouse.containsMouse ? Qt.lighter(Theme.accent, 1.2) : Theme.accent
                        }

                        MouseArea {
                            id: volSliderMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor

                            function updateVol(mouseEvent) {
                                var r = Math.max(0, Math.min(1, mouseEvent.x / width))
                                root.setVolume(r)
                            }

                            onPressed: (mouse) => updateVol(mouse)
                            onPositionChanged: (mouse) => {
                                if (pressed) updateVol(mouse)
                            }
                        }
                    }
                }

                // Loop button
                Rectangle {
                    width: 28
                    height: 28
                    radius: 14
                    color: root.loop
                        ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.22)
                        : (loopBtnMouse.containsMouse
                            ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.12)
                            : "transparent")

                    Behavior on color { ColorAnimation { duration: 120 } }

                    IconRefreshCw {
                        anchors.centerIn: parent
                        size: 14
                        color: root.loop ? Theme.accent : (loopBtnMouse.containsMouse ? Theme.text : Theme.muted)
                    }

                    MouseArea {
                        id: loopBtnMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleLoop()
                    }
                }
            }
        }
    }
}
