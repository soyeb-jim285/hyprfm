import QtQuick

MouseArea {
    id: root

    required property var flickable

    property real wheelStep: 56
    property real mouseWheelMultiplier: 1.0
    property real touchpadMultiplier: 1.35
    property real minVelocity: 180
    property real maxVelocity: 5200
    property real kineticGain: 1.35
    property real smoothing: 0.72
    property real edgeEpsilon: 1.0
    // Browser-style overscroll: larger visible rubber-band, softer
    // resistance, and a springy settle on release.
    property real overshootLimit: 140
    property real overshootResistance: 0.38
    property real maxOvershootInput: 520
    // Bounce magnitude per unit of impact velocity. Keep this deliberately
    // small: slow edge hits should settle, not fire a visibly faster bounce.
    property real kineticBounceGain: 0.018
    property real maxKineticBounceInput: 90
    // Minimum impact velocity that actually produces a visible bounce.
    // Very slow landings skip bounce entirely — matches what browsers do
    // when a fling decays to near-zero before reaching the edge.
    property real minBounceVelocity: 260

    property real velocity: 0
    property double lastWheelTimestamp: 0
    property bool kineticCandidate: false
    property real overshootInput: 0
    property real overshootReleaseVelocity: 0
    property real pendingKineticBounceVelocity: 0
    // Set by runKineticGlide when the glide will clamp against an edge.
    // It is the glide velocity at the instant of impact, which drives the
    // elastic bounce amount and duration — browser-style: slow impact
    // makes a small gentle bounce; fast impact makes a big springy one.
    property real pendingImpactVelocity: 0

    // Browser-style exponential-feeling glide replaces the linear
    // flickDeceleration decay. Tail uses OutQuart — soft landing.
    property real kineticTau: 0.42

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
            || (wheel.phase !== Qt.NoScrollPhase && wheel.angleDelta.y === 0)
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
        pendingImpactVelocity = 0
        overshootReleaseVelocity = 0
        finishTimer.stop()
        kineticAnim.stop()
        kineticBounceSequence.stop()
        bounceBackAnimation.stop()
    }

    function bounceBack() {
        if (!hasOvershoot()) {
            setOvershootInput(0)
            overshootReleaseVelocity = 0
            return
        }

        var mag = Math.min(1.0, Math.abs(overshootInput) / maxOvershootInput)
        var speed = Math.abs(overshootReleaseVelocity)
        var duration = speed >= minBounceVelocity
            ? Math.round(Math.max(280, Math.min(620, Math.abs(overshootInput) / speed * 1600)))
            : Math.round(380 + mag * 260)

        kineticBounceSequence.stop()
        bounceBackAnimation.stop()
        bounceBackAnimation.duration = duration
        bounceBackAnimation.from = overshootInput
        bounceBackAnimation.to = 0
        overshootReleaseVelocity = 0
        bounceBackAnimation.start()
    }

    function triggerKineticBounce(contentVelocity) {
        if (!flickable || contentVelocity === 0)
            return

        var atTop = flickable.contentY <= minimumContentY() + edgeEpsilon
        var atBottom = flickable.contentY >= maximumContentY() - edgeEpsilon
        if ((contentVelocity < 0 && !atTop) || (contentVelocity > 0 && !atBottom))
            return

        var speed = Math.abs(contentVelocity)
        var effectiveVelocity = contentVelocity
        if (speed < minBounceVelocity)
            effectiveVelocity = (contentVelocity < 0 ? -1 : 1) * Math.max(90, speed * 0.6)

        var target = Math.max(-maxKineticBounceInput,
                              Math.min(maxKineticBounceInput, effectiveVelocity * kineticBounceGain))
        if (Math.abs(target) < 2.5)
            target = contentVelocity < 0 ? -2.5 : 2.5

        var mag = Math.min(1.0, Math.abs(target) / maxKineticBounceInput)
        var inDur  = Math.round(150 + mag * 120)
        var outDur = Math.round(320 + mag * 300)

        kineticBounceSequence.stop()
        bounceBackAnimation.stop()
        overshootReleaseVelocity = 0
        overshootInput = 0
        kineticBounceIn.from = 0
        kineticBounceIn.to = target
        kineticBounceIn.duration = inDur
        kineticBounceOut.from = target
        kineticBounceOut.to = 0
        kineticBounceOut.duration = outDur
        kineticBounceSequence.start()
    }

    function stopAndSettle() {
        kineticAnim.stop()
        if (flickable && flickable.flicking)
            flickable.cancelFlick()

        resetState()
        bounceBack()
    }

    function runKineticGlide(contentVelocity) {
        // contentVelocity > 0 means the content should scroll downward
        // (contentY increases), matching deltaFor()'s sign convention.
        if (!flickable || contentVelocity === 0) {
            pendingImpactVelocity = 0
            return false
        }

        var current = flickable.contentY
        var unclampedTarget = current + contentVelocity * kineticTau
        var target = boundedContentY(unclampedTarget)
        var distUnclamped = Math.abs(unclampedTarget - current)
        var distActual = Math.abs(target - current)
        if (distActual < 4) {
            pendingImpactVelocity = 0
            return false
        }

        // If the glide is clipped by an edge, compute the velocity at the
        // moment of impact using the OutQuart velocity profile. That is
        // the velocity the bounce should feed on — gentle impacts get
        // gentle bounces, hard impacts get big springy ones.
        var hitsEdge = distActual + 0.5 < distUnclamped
        if (hitsEdge && distUnclamped > 0) {
            var f = distActual / distUnclamped        // fraction covered
            // OutQuart: y = 1 - (1-x)^4, so velocity fraction at distance
            // fraction f equals (1-f)^(3/4) (times the constant peak).
            var velFraction = Math.pow(Math.max(0, 1 - f), 0.75)
            pendingImpactVelocity = contentVelocity * velFraction
        } else {
            pendingImpactVelocity = 0
        }

        kineticAnim.stop()
        kineticAnim.from = current
        kineticAnim.to = target
        // Duration from distance and velocity; clamped so short flicks
        // stay snappy and long flicks don't run forever.
        kineticAnim.duration = Math.round(
            Math.min(1400, Math.max(180,
                distActual / Math.abs(contentVelocity) * 1800)))
        kineticAnim.start()
        return true
    }

    function atEdgeFor(contentVelocity) {
        if (!flickable) return false
        if (contentVelocity > 0)
            return flickable.contentY >= maximumContentY() - edgeEpsilon
        if (contentVelocity < 0)
            return flickable.contentY <= minimumContentY() + edgeEpsilon
        return false
    }

    function finishScroll() {
        if (!flickable)
            return

        finishTimer.stop()

        if (hasOvershoot()) {
            velocity = 0
            lastWheelTimestamp = 0
            kineticCandidate = false
            pendingKineticBounceVelocity = 0
            pendingImpactVelocity = 0
            kineticAnim.stop()
            bounceBack()
            return
        }

        var rawContentVelocity = Math.max(-maxVelocity, Math.min(maxVelocity, velocity * kineticGain))
        var contentVelocity = clampedContentVelocity(rawContentVelocity)
        var useKinetic = kineticCandidate && Math.abs(rawContentVelocity) >= minVelocity

        velocity = 0
        lastWheelTimestamp = 0
        kineticCandidate = false

        if (useKinetic) {
            if (atEdgeFor(rawContentVelocity)) {
                // Already at the edge — fling bounces immediately.
                pendingKineticBounceVelocity = 0
                triggerKineticBounce(rawContentVelocity)
            } else if (Math.abs(contentVelocity) >= minVelocity && runKineticGlide(contentVelocity)) {
                // Glide started — bounce fires on kineticAnim.onFinished
                // if it lands at an edge.
                pendingKineticBounceVelocity = contentVelocity
            } else {
                pendingKineticBounceVelocity = 0
                flickable.returnToBounds()
            }
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

        // Cancel any in-flight glide — user input always wins, position
        // stays where it is, then new delta is applied from there.
        if (kineticAnim.running) {
            pendingKineticBounceVelocity = 0
            pendingImpactVelocity = 0
            kineticAnim.stop()
        }
        kineticBounceSequence.stop()
        bounceBackAnimation.stop()
        if (flickable.flicking) {
            pendingKineticBounceVelocity = 0
            pendingImpactVelocity = 0
            flickable.cancelFlick()
        }

        var now = Date.now()
        var gap = lastWheelTimestamp > 0 ? now - lastWheelTimestamp : 16
        var dt = Math.max(8, Math.min(24, gap))
        var instantVelocity = (delta / dt) * 1000
        var isTouchpad = wheel.pixelDelta.y !== 0
            || (wheel.phase !== Qt.NoScrollPhase && wheel.angleDelta.y === 0)

        kineticCandidate = isTouchpad && (kineticCandidate || shouldUseKinetic(wheel, gap))
        scrollStarted()
        scrollBy(delta)

        if (hasOvershoot()) {
            overshootReleaseVelocity = Math.max(-maxVelocity,
                                                Math.min(maxVelocity, isTouchpad ? instantVelocity : 0))
            velocity = 0
            kineticCandidate = false
            lastWheelTimestamp = now
            finishTimer.restart()
            wheel.accepted = true
            return
        }

        velocity = isTouchpad
            ? (lastWheelTimestamp <= 0
                ? instantVelocity
                : velocity * (1.0 - smoothing) + instantVelocity * smoothing)
            : 0
        lastWheelTimestamp = now

        if (isTouchpad || hasOvershoot())
            finishTimer.restart()
        else
            finishTimer.stop()

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
        id: kineticAnim
        target: root.flickable
        property: "contentY"
        easing.type: Easing.OutQuart
        onFinished: {
            // Prefer the impact-time velocity (computed in runKineticGlide)
            // so the bounce matches the residual energy at the moment the
            // glide hit the edge — slow landings → soft bounce, fast
            // landings → big springy bounce.
            var bv = Math.abs(root.pendingImpactVelocity) > root.edgeEpsilon
                    ? root.pendingImpactVelocity
                    : root.pendingKineticBounceVelocity
            root.pendingImpactVelocity = 0
            root.pendingKineticBounceVelocity = 0
            if (Math.abs(bv) > root.edgeEpsilon)
                root.triggerKineticBounce(bv)
        }
    }

    NumberAnimation {
        id: bounceBackAnimation
        target: root
        property: "overshootInput"
        easing.type: Easing.OutCubic
    }

    SequentialAnimation {
        id: kineticBounceSequence

        NumberAnimation {
            id: kineticBounceIn
            target: root
            property: "overshootInput"
            easing.type: Easing.OutCubic
        }

        NumberAnimation {
            id: kineticBounceOut
            target: root
            property: "overshootInput"
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
