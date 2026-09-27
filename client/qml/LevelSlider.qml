// A front-panel control bound to one CAT setting: AF, RF, SQL, MIC, PWR.
// It follows the radio while untouched, and sends while it is being moved —
// the server keeps only the latest position of a knob turned quickly.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

RowLayout {
    id: ctl
    property string code
    property string label
    readonly property var d: Radio.describe(code)
    readonly property var p: (d.params && d.params.length > 0) ? d.params[0] : ({min: 0, max: 100, step: 1})
    readonly property int rev: Radio.revision
    property int lastSent: -1
    spacing: 6

    Label {
        text: ctl.label
        color: Ui.text
        font.bold: true
        Layout.preferredWidth: 40
    }
    Slider {
        id: s
        Layout.fillWidth: true
        from: ctl.p.min
        to: ctl.p.max
        stepSize: 1
        enabled: Radio.connected && Radio.radioOn
        onMoved: throttle.restart()
        onPressedChanged: if (!pressed) ctl.push()
        Binding {
            target: s; property: "value"
            value: ctl.rev >= 0 ? Radio.paramInt(ctl.code, 0) : ctl.p.min
            when: !s.pressed && !throttle.running
            restoreMode: Binding.RestoreNone
        }
    }
    Label {
        text: Math.round(s.value)
        color: s.pressed ? Ui.accent : Ui.dim
        font.family: Ui.mono
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: 34
    }
    Timer {
        id: throttle
        interval: 90
        onTriggered: ctl.push()
    }
    function push() {
        const v = Math.round(s.value)
        if (v === lastSent && !s.pressed) return
        lastSent = v
        Radio.setParam(code, 0, String(v))
    }
}
