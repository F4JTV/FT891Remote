// The radio's display: frequency, mode, status flags and meters.
//
// The frequency is a tuning surface. A mouse wheel or a touchpad reports
// detents to Radio.tune(); a horizontal drag does the same on a touch screen.
// A drawn knob, later, will only need to call the same function.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

Rectangle {
    id: lcd
    color: Ui.lcdBack
    radius: 10
    border.color: "#070b0d"
    border.width: 3
    implicitHeight: col.implicitHeight + 20
    property bool compact: false

    readonly property int rev: Radio.revision
    function flag(code) { return rev >= 0 && Radio.isOn(code) }
    // A badge for one value of a choice: IPO is lit when PA0 is 0 — IPO
    // selected — not when it is "on" (1 is AMP), which lit it the wrong way
    // round.
    function valueIs(code, value) { return rev >= 0 && Radio.hasValue(code) && Radio.paramValue(code, 0) === value }

    // Index, counted from the right and ignoring the dots, of the digit the
    // current step moves: the underlined one.
    readonly property int stepDigit: {
        const s = Radio.stepHz
        return s >= 100000 ? 5 : s >= 10000 ? 4 : s >= 1000 ? 3 : s >= 100 ? 2 : s >= 10 ? 1 : 0
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: 10
        spacing: 4

        // ------------------------------------------------ status flags
        Flow {
            Layout.fillWidth: true
            spacing: 6
            component Badge: Rectangle {
                property string t
                property bool on: true
                property color c: Ui.lcdText
                visible: on
                radius: 3
                color: "transparent"
                border.color: c
                border.width: 1
                implicitWidth: bt.implicitWidth + 8
                implicitHeight: 18
                Text {
                    id: bt
                    anchors.centerIn: parent
                    text: parent.t
                    color: parent.c
                    font.family: Ui.mono
                    font.pixelSize: 11
                    font.bold: true
                }
            }
            Badge { t: Radio.ptt ? "TX" : "RX"; c: Radio.ptt ? Ui.tx : Ui.ok; on: Radio.radioOn }
            Badge { t: Radio.memMode === "VFO" ? "VFO-A" : Radio.memMode + " " + Radio.memName; on: Radio.radioOn }
            Badge { t: Radio.mode; on: Radio.mode.length > 0; c: Ui.accent }
            Badge { t: "SPL"; on: Radio.split }
            Badge { t: "CLAR " + (Radio.clarOffset > 0 ? "+" : "") + Radio.clarOffset; on: Radio.rxClar || Radio.txClar }
            Badge { t: "NAR"; on: lcd.flag("NAR") }
            Badge { t: "IPO"; on: lcd.valueIs("IPO", "0") }
            Badge { t: "ATT"; on: lcd.flag("ATT") }
            Badge { t: "NB"; on: lcd.flag("NB") }
            Badge { t: "DNR"; on: lcd.flag("DNR") }
            Badge { t: "DNF"; on: lcd.flag("DNF") }
            Badge { t: "NCH"; on: lcd.flag("NCH") }
            Badge { t: "CNT"; on: lcd.flag("CNT") }
            Badge { t: "PROC"; on: lcd.flag("PRC") }
            Badge { t: "VOX"; on: lcd.flag("VOX") }
            Badge { t: "MON"; on: lcd.flag("MON") }
            Badge { t: "TNR"; on: lcd.flag("TNR") }
            Badge { t: "LOCK"; on: lcd.flag("LOCK"); c: Ui.warn }
            Badge { t: "FAST"; on: lcd.flag("FS") }
            Badge { t: "AGC " + (lcd.rev >= 0 ? Radio.display("AGC") : ""); on: lcd.rev >= 0 && Radio.hasValue("AGC") }
            Badge { t: "HI SWR"; on: Radio.hiSwr; c: Ui.tx }
            Badge { t: "OUT OF BAND"; on: Radio.radioOn && !Radio.txAllowed; c: Ui.warn }
            Badge { t: "TUNING"; on: Radio.tuning; c: Ui.warn }
            Badge { t: "KEYING"; on: Radio.cwBusy; c: Ui.warn }
        }

        // --------------------------------------------------- frequency
        Item {
            id: freqArea
            Layout.fillWidth: true
            implicitHeight: lcd.compact ? 56 : 76

            Row {
                id: digits
                anchors.centerIn: parent
                spacing: 0
                readonly property string txt: Radio.freqText
                readonly property int px: lcd.compact ? 44 : 60
                Repeater {
                    model: digits.txt.length
                    Text {
                        readonly property string ch: digits.txt.charAt(index)
                        // Position from the right among the digits only.
                        readonly property int pos: {
                            let n = 0
                            for (let i = digits.txt.length - 1; i > index; --i)
                                if (digits.txt.charAt(i) !== ".") ++n
                            return n
                        }
                        text: ch
                        color: Radio.radioOn ? (Radio.knobActive ? "#b6f3ff" : Ui.lcdText) : Ui.lcdDim
                        font.family: Ui.mono
                        font.pixelSize: ch === "." ? digits.px * 0.6 : digits.px
                        font.bold: true
                        Rectangle {
                            visible: parent.ch !== "." && parent.pos === lcd.stepDigit && Radio.radioOn
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width * 0.8
                            height: 3
                            color: Ui.accent
                        }
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                property real wheelAcc: 0
                property real pressX: 0
                property real lastX: 0
                property bool dragged: false
                onWheel: (w) => {
                    // Mice report 120 per detent; touchpads report less,
                    // more often: accumulate until a full detent.
                    wheelAcc += w.angleDelta.y !== 0 ? w.angleDelta.y : w.angleDelta.x
                    const n = Math.trunc(wheelAcc / 120)
                    if (n !== 0) {
                        wheelAcc -= n * 120
                        Radio.tune((w.modifiers & Qt.ShiftModifier) ? n * 10 : n)
                    }
                }
                onPressed: (m) => { pressX = m.x; lastX = m.x; dragged = false }
                onPositionChanged: (m) => {
                    const d = m.x - lastX
                    const n = Math.trunc(d / 14)
                    if (n !== 0) {
                        dragged = true
                        lastX += n * 14
                        Radio.tune(n)
                    }
                }
                onClicked: if (!dragged) Ui.frequencyEntryRequested(false)
            }
        }

        // ------------------------------------------- VFO-B and filters
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Text {
                text: "B " + Radio.freqBText
                color: Radio.split ? Ui.accent : Ui.lcdText
                font.family: Ui.mono
                font.pixelSize: 15
                MouseArea { anchors.fill: parent; onClicked: Ui.frequencyEntryRequested(true) }
            }
            Text {
                text: "WIDTH " + Radio.widthText
                visible: lcd.rev >= 0 && Radio.hasValue("WDH") && Radio.widthAvailable
                color: Ui.lcdText
                font.family: Ui.mono
                font.pixelSize: 13
            }
            Text {
                text: "SHIFT " + (lcd.rev >= 0 ? Radio.describeCurrent("SFT", 1) : "")
                // IF shift has no switch: shown when it moves the passband.
                visible: lcd.rev >= 0 && Radio.isOn("SFT") && Radio.paramInt("SFT", 1) !== 0
                color: Ui.lcdText
                font.family: Ui.mono
                font.pixelSize: 13
            }
            Item { Layout.fillWidth: true }
            Text {
                text: Radio.stepHz >= 1000 ? (Radio.stepHz / 1000) + " kHz" : Radio.stepHz + " Hz"
                color: Ui.accent
                font.family: Ui.mono
                font.pixelSize: 13
            }
        }

        // ------------------------------------------------------ meters
        MeterBar {
            Layout.fillWidth: true
            visible: !Radio.ptt
            label: "S"
            value: Radio.sMeter
            valueText: Radio.radioOn ? Radio.sMeterText : ""
            redFrom: 0.5
        }
        MeterBar {
            Layout.fillWidth: true
            visible: Radio.ptt
            label: "PO"
            value: Radio.poMeter
            valueText: Radio.poText
            redFrom: 1.0
        }
        MeterBar {
            Layout.fillWidth: true
            visible: Radio.ptt
            label: "SWR"
            value: Radio.swrMeter
            valueText: Radio.swrText
            redFrom: 0.5
        }
        RowLayout {
            Layout.fillWidth: true
            visible: Radio.ptt && !lcd.compact
            spacing: 12
            MeterBar { Layout.fillWidth: true; label: "ALC"; value: Radio.alcMeter; valueText: ""; segments: 15; redFrom: 0.6 }
            MeterBar { Layout.fillWidth: true; label: "COMP"; value: Radio.compMeter; valueText: Radio.compText; segments: 15; redFrom: 0.8 }
        }

        Text {
            Layout.fillWidth: true
            visible: Radio.radioStatus.length > 0
            text: Radio.radioStatus
            color: Ui.warn
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
