import QtQuick
import SeventhSlide

// A small ring that fills clockwise from the top as ``value`` goes from 0 to
// 1: work going on, said without words (the bottom bar's background work).
Canvas {
    id: ring

    property real value: 0
    property real lineWidth: 2
    property color track: Style.controlEdge
    property color fill: Style.focusEdge

    implicitWidth: 13
    implicitHeight: 13

    onValueChanged: ring.requestPaint()
    onTrackChanged: ring.requestPaint()
    onFillChanged: ring.requestPaint()
    onWidthChanged: ring.requestPaint()
    onHeightChanged: ring.requestPaint()

    onPaint: {
        const ctx = ring.getContext("2d")
        ctx.reset()
        const r = Math.min(ring.width, ring.height) / 2 - ring.lineWidth / 2
        if (r <= 0)
            return
        const cx = ring.width / 2
        const cy = ring.height / 2
        ctx.lineWidth = ring.lineWidth
        ctx.strokeStyle = ring.track
        ctx.beginPath()
        ctx.arc(cx, cy, r, 0, 2 * Math.PI)
        ctx.stroke()
        const v = Math.max(0, Math.min(1, ring.value))
        if (v <= 0)
            return
        ctx.strokeStyle = ring.fill
        ctx.beginPath()
        ctx.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * v)
        ctx.stroke()
    }
}
