// Small icons drawn with a Canvas rather than taken from a font.
//
// Symbol characters depend on the fonts of the system: on Android « ⟳ » has
// no glyph at all and shows as an empty box, and « ◀ ▶ » have an emoji form
// that Android prefers, drawn as orange squares. Drawn shapes look the same
// everywhere and follow the key's colour.
import QtQuick

Canvas {
    id: icon
    // refresh, left, right, fastLeft, fastRight, play
    property string name: "refresh"
    property color color: Ui.text
    implicitWidth: 18
    implicitHeight: 18

    onNameChanged: requestPaint()
    onColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const s = Math.min(width, height)
        if (s <= 0) return
        const cx = width / 2, cy = height / 2
        ctx.fillStyle = icon.color
        ctx.strokeStyle = icon.color
        ctx.lineWidth = Math.max(1.5, s * 0.12)
        ctx.lineCap = "round"
        ctx.lineJoin = "round"

        // A solid triangle pointing left (dir -1) or right (dir 1), centred
        // on x, of the given height.
        function triangle(x, dir, h) {
            const w = h * 0.8
            ctx.beginPath()
            ctx.moveTo(x + dir * w / 2, cy)
            ctx.lineTo(x - dir * w / 2, cy - h / 2)
            ctx.lineTo(x - dir * w / 2, cy + h / 2)
            ctx.closePath()
            ctx.fill()
        }

        switch (icon.name) {
        case "left":      triangle(cx, -1, s * 0.7); break
        case "right":
        case "play":      triangle(cx,  1, s * 0.7); break
        case "fastLeft":  triangle(cx - s * 0.22, -1, s * 0.6); triangle(cx + s * 0.22, -1, s * 0.6); break
        case "fastRight": triangle(cx - s * 0.22,  1, s * 0.6); triangle(cx + s * 0.22,  1, s * 0.6); break
        case "refresh": {
            // An open circle turning clockwise, with its arrowhead.
            // A quarter of the circle is left open at the top right, so that
            // it reads as a turn and not as a ring.
            const r = s * 0.33
            const start = -Math.PI * 0.12, end = Math.PI * 1.38
            ctx.beginPath()
            ctx.arc(cx, cy, r, start, end, false)
            ctx.stroke()
            const ex = cx + r * Math.cos(end), ey = cy + r * Math.sin(end)
            const a = s * 0.27
            // Tangent at the end of a clockwise arc points along +90°.
            const t = end + Math.PI / 2
            ctx.beginPath()
            ctx.moveTo(ex + a * Math.cos(t), ey + a * Math.sin(t))
            ctx.lineTo(ex + a * Math.cos(t + 2.4), ey + a * Math.sin(t + 2.4))
            ctx.lineTo(ex + a * Math.cos(t - 2.4), ey + a * Math.sin(t - 2.4))
            ctx.closePath()
            ctx.fill()
            break
        }
        }
    }
}
