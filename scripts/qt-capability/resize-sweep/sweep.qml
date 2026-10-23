import QtQuick
import org.kde.kwin

// Sweeps the target window's width like an interactive edge drag and records when KWin
// applies each size (that happens only after the client acked and committed it).
Item {
    id: root
    property var target: null
    property var applied: []
    property int step: 0
    property real started: 0
    property var base
    function findTarget() {
        const wanted = "%TARGET%"
        for (const w of Workspace.windows)
            if (w.normalWindow && (w.resourceClass.indexOf(wanted) >= 0 || w.resourceName.indexOf(wanted) >= 0)) return w
        return null
    }
    Connections {
        target: root.target
        function onFrameGeometryChanged() {
            if (root.started > 0 && root.step <= 240) root.applied.push([Date.now() - root.started, root.target.frameGeometry.width])
        }
    }
    Timer {
        id: wait; interval: 200; repeat: true; running: true
        onTriggered: {
            root.target = root.findTarget()
            if (!root.target) return
            running = false
            root.target.frameGeometry = Qt.rect(40, 40, 900, 640)
            settle.start()
        }
    }
    Timer { id: settle; interval: 1500; onTriggered: { root.started = Date.now(); sweep.start() } }
    // ~ a 125 Hz pointer: 240 steps over ~2 s, 900 -> 1380 -> 900 px wide.
    Timer {
        id: sweep; interval: 8; repeat: true
        onTriggered: {
            if (root.step === 240) { stop(); finish.start(); return }
            const phase = root.step % 120, ramp = phase < 60 ? phase : 120 - phase
            ++root.step
            root.target.frameGeometry = Qt.rect(40, 40, 900 + ramp * 8, 640)
        }
    }
    Timer {
        id: finish; interval: 600
        onTriggered: {
            const t = root.applied.map(a => a[0])
            const gaps = []
            for (let i = 1; i < t.length; ++i) gaps.push(t[i] - t[i - 1])
            gaps.sort((a, b) => a - b)
            const pick = q => gaps.length ? gaps[Math.min(gaps.length - 1, Math.floor(gaps.length * q))] : -1
            console.warn("ABDM-SWEEP " + JSON.stringify({target: "%TARGET%", requested: root.step, applied: t.length,
                durationMs: t.length ? t[t.length - 1] : 0, gapP50: pick(0.5), gapP95: pick(0.95), gapMax: pick(1)}))
        }
    }
}
