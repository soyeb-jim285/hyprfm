import QtQuick

MouseArea {
    id: root

    required property var flickable

    property real wheelStep: 56
    property real mouseWheelMultiplier: 1.0
    property real touchpadMultiplier: 1.0
    property real minVelocity: 180
    property real maxVelocity: 5200
    property real kineticGain: 1.35
    property real smoothing: 0.72
    property real edgeEpsilon: 1.0
    property real overshootLimit: 72
    property real overshootResistance: 0.55
    property real maxOvershootInput: 320
    property real kineticBounceGain: 0.035
    property real maxKineticBounceInput: 180

    property real velocity: 0
    property double lastWheelTimestamp: 0
    property bool kineticCandidate: false
    property real overshootInput: 0
    property real pendingKineticBounceVelocity: 0

    readonly property real visualOvershoot: overshootInput < 0
        ? rubberDistance(-overshootInput)
        : -rubberDistance(overshootInput)

    signal scrollStarted()

    acceptedButtons: Qt.NoButton
    preventStealing: true
    scrollGestureEnabled: true

    function minimumContentY() {
        if (!flickable)
            return 0

        return flickable.originY
    }

    function maximumContentY() {
        if (!flickable)
            return 0

        return Math.max(minimumContentY(), minimumContentY() + flickable.contentHeight - flickable.height)
    }

    function boundedContentY(value) {
        return Math.max(minimumContentY(), Math.min(maximumContentY(), value))
    }

    function rubberDistance(distance) {
        if (distance <= 0)
            return 0

        var scaled = (distance * overshootResistance) / overshootLimit
        return overshootLimit * (1 - 1 / (1 + scaled))
    }

    function setOvershootInput(value) {
        overshootInput = Math.max(-maxOvershootInput, Math.min(maxOvershootInput, value))
    }

    function hasOvershoot() {
        return Math.abs(overshootInput) > edgeEpsilon
    }

    function absorbOvershoot(delta) {
        if (!hasOvershoot())
            return delta

        var sameDirection = (overshootInput < 0 && delta < 0)
            || (overshootInput > 0 && delta > 0)

        if (sameDirection) {
            setOvershootInput(overshootInput + delta)
            return 0
        }

        var combined = overshootInput + delta
        if ((overshootInput < 0 && combined < 0)
                || (overshootInput > 0 && combined > 0)) {
            setOvershootInput(combined)
            return 0
        }

        setOvershootInput(0)
        return combined
    }

    function deltaFor(wheel) {
        var isTouchpad = wheel.pixelDelta.y !== 0
        var rawDelta = isTouchpad
            ? wheel.pixelDelta.y * touchpadMultiplier
            : (wheel.angleDelta.y / 120.0) * wheelStep * mouseWheelMultiplier

        return wheel.inverted ? rawDelta : -rawDelta
    }

    function shouldUseKinetic(wheel, gap) {
        return wheel.pixelDelta.y !== 0
            || wheel.phase !== Qt.NoScrollPhase
            || Math.abs(wheel.angleDelta.y) < 120
            || gap < 50
    }

    function scrollBy(delta) {
        if (!flickable || Math.abs(delta) < 0.01)
            return

        kineticBounceSequence.stop()
        bounceBackAnimation.stop()

        var remainingDelta = absorbOvershoot(delta)
        if (Math.abs(remainingDelta) < 0.01)
            return

        var minY = minimumContentY()
        var maxY = maximumContentY()
        var next = flickable.contentY + remainingDelta

        if (next < minY) {
            flickable.contentY = minY
            setOvershootInput(overshootInput + next - minY)
            return
        }

        if (next > maxY) {
            flickable.contentY = maxY
            setOvershootInput(overshootInput + next - maxY)
            return
        }

        flickable.contentY = next
    }

    function remainingDistanceFor(velocityValue) {
        if (!flickable)
            return 0

        if (velocityValue > 0)
            return Math.max(0, maximumContentY() - flickable.contentY)

        if (velocityValue < 0)
            return Math.max(0, flickable.contentY - minimumContentY())

        return 0
    }

    function clampedContentVelocity(velocityValue) {
        if (!flickable || velocityValue === 0)
            return 0

        var remaining = remainingDistanceFor(velocityValue)
        if (remaining <= edgeEpsilon)
            return 0

        var maxAllowed = Math.sqrt(2 * flickable.flickDeceleration * remaining)
        var magnitude = Math.min(Math.abs(velocityValue), maxAllowed)
        return velocityValue < 0 ? -magnitude : magnitude
    }

    function resetState() {
        velocity = 0
        lastWheelTimestamp = 0
        kineticCandidate = false
        pendingKineticBounceVelocity = 0
        finishTimer.stop()
    }

    function bounceBack() {
        if (!hasOvershoot()) {
            setOvershootInput(0)
            return
        }

        bounceBackAnimation.stop()
        bounceBackAnimation.from = overshootInput
        bounceBackAnimation.to = 0
        bounceBackAnimation.start()
    }

    function triggerKineticBounce(contentVelocity) {
        if (!flickable || contentVelocity === 0)
            return

        var atTop = flickable.contentY <= minimumContentY() + edgeEpsilon
        var atBottom = flickable.contentY >= maximumContentY() - edgeEpsilon
        if ((contentVelocity < 0 && !atTop) || (contentVelocity > 0 && !atBottom))
            return

        var target = Math.max(-maxKineticBounceInput,
                              Math.min(maxKineticBounceInput, contentVelocity * kineticBounceGain))
        if (Math.abs(target) <= edgeEpsilon)
            return

        kineticBounceSequence.stop()
        bounceBackAnimation.stop()
        overshootInput = 0
        kineticBounceIn.from = 0
        kineticBounceIn.to = target
        kineticBounceOut.from = target
        kineticBounceOut.to = 0
        kineticBounceSequence.start()
    }

    function stopAndSettle() {
        if (flickable && flickable.flicking)
            flickable.cancelFlick()

        resetState()
        bounceBack()
    }

    function finishScroll() {
        if (!flickable)
            return

        finishTimer.stop()

        if (hasOvershoot()) {
            resetState()
            bounceBack()
            return
        }

        var contentVelocity = Math.max(-maxVelocity, Math.min(maxVelocity, velocity * kineticGain))
        contentVelocity = clampedContentVelocity(contentVelocity)
        var useKinetic = kineticCandidate && Math.abs(contentVelocity) >= minVelocity

        velocity = 0
        lastWheelTimestamp = 0
        kineticCandidate = false

        if (useKinetic) {
            pendingKineticBounceVelocity = contentVelocity
            flickable.flick(0, -contentVelocity)
        } else {
            pendingKineticBounceVelocity = 0
            flickable.returnToBounds()
        }
    }

    onWheel: (wheel) => {
        if (!enabled || !visible || !flickable || !flickable.visible || !flickable.interactive) {
            wheel.accepted = false
            return
        }

        if (wheel.modifiers !== Qt.NoModifier) {
            wheel.accepted = false
            return
        }

        if (maximumContentY() <= minimumContentY()) {
            wheel.accepted = false
            return
        }

        var delta = deltaFor(wheel)
        if (delta === 0) {
            wheel.accepted = false
            return
        }

        if (flickable.flicking) {
            pendingKineticBounceVelocity = 0
            flickable.cancelFlick()
        }

        var now = Date.now()
        var gap = lastWheelTimestamp > 0 ? now - lastWheelTimestamp : 16
        var dt = Math.max(8, Math.min(24, gap))
        var instantVelocity = (delta / dt) * 1000

        kineticCandidate = kineticCandidate || shouldUseKinetic(wheel, gap)
        scrollStarted()
        scrollBy(delta)

        if (hasOvershoot()) {
            velocity = 0
            kineticCandidate = false
            lastWheelTimestamp = now
            finishTimer.restart()
            wheel.accepted = true
            return
        }

        velocity = lastWheelTimestamp <= 0
            ? instantVelocity
            : velocity * (1.0 - smoothing) + instantVelocity * smoothing
        lastWheelTimestamp = now

        finishTimer.restart()

        if (wheel.phase === Qt.ScrollEnd)
            finishScroll()

        wheel.accepted = true
    }

    Timer {
        id: finishTimer
        interval: 45
        repeat: false
        onTriggered: root.finishScroll()
    }

    NumberAnimation {
        id: bounceBackAnimation
        target: root
        property: "overshootInput"
        duration: 260
        easing.type: Easing.OutCubic
    }

    SequentialAnimation {
        id: kineticBounceSequence

        NumberAnimation {
            id: kineticBounceIn
            target: root
            property: "overshootInput"
            duration: 90
            easing.type: Easing.OutQuad
        }

        NumberAnimation {
            id: kineticBounceOut
            target: root
            property: "overshootInput"
            duration: 230
            easing.type: Easing.OutCubic
        }
    }

    Translate {
        id: overshootTranslate
        y: root.visualOvershoot
    }

    Connections {
        target: root.flickable

        function onInteractiveChanged() {
            if (!root.flickable.interactive)
                root.stopAndSettle()
        }

        function onContentHeightChanged() {
            if (!root.flickable.moving) {
                root.flickable.contentY = root.boundedContentY(root.flickable.contentY)
                root.bounceBack()
            }
        }

        function onHeightChanged() {
            if (!root.flickable.moving) {
                root.flickable.contentY = root.boundedContentY(root.flickable.contentY)
                root.bounceBack()
            }
        }

        function onVisibleChanged() {
            if (!root.flickable.visible)
                root.stopAndSettle()
        }

        function onMovementEnded() {
            if (Math.abs(root.pendingKineticBounceVelocity) > root.edgeEpsilon) {
                var bounceVelocity = root.pendingKineticBounceVelocity
                root.pendingKineticBounceVelocity = 0
                root.triggerKineticBounce(bounceVelocity)
            }
        }
    }

    Component.onCompleted: {
        if (!flickable)
            return

        flickable.flickDeceleration = 2800
        flickable.maximumFlickVelocity = Math.max(flickable.maximumFlickVelocity, maxVelocity)
        flickable.contentItem.transform = [overshootTranslate]
    }
}
