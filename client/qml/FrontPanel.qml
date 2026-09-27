// The keys and knobs of the FT-891's face: tuning, band and mode, VFO keys,
// clarifier, and the level controls.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

ColumnLayout {
    id: fp
    spacing: 8
    readonly property int rev: Radio.revision
    readonly property bool live: Radio.connected && Radio.radioOn
    function on(code) { return rev >= 0 && Radio.isOn(code) }

    // ---------------------------------------------------------- tuning
    RowLayout {
        Layout.fillWidth: true
        spacing: 4
        PanelKey { glyph: "fastLeft";  implicitWidth: 48; enabled: fp.live; autoRepeat: true; onClicked: Radio.tune(-10) }
        PanelKey { glyph: "left";      implicitWidth: 48; enabled: fp.live; autoRepeat: true; onClicked: Radio.tune(-1) }
        ComboBox {
            id: stepBox
            Layout.fillWidth: true
            // The height of the keys beside it. Material adds insets that
            // made it a third taller than they are.
            topInset: 0
            bottomInset: 0
            implicitHeight: 48
            model: Radio.tuneSteps.map(function (s) { return s >= 1000 ? (s / 1000) + " kHz" : s + " Hz" })
            currentIndex: Radio.stepIndex
            onActivated: (i) => Radio.stepIndex = i
        }
        PanelKey { glyph: "right";     implicitWidth: 48; enabled: fp.live; autoRepeat: true; onClicked: Radio.tune(1) }
        PanelKey { glyph: "fastRight"; implicitWidth: 48; enabled: fp.live; autoRepeat: true; onClicked: Radio.tune(10) }
    }

    // ------------------------------------------------ band, mode, VFO
    GridLayout {
        Layout.fillWidth: true
        columns: 4
        columnSpacing: 6
        rowSpacing: 6

        PanelKey {
            Layout.fillWidth: true
            label: "BAND ▼"; enabled: fp.live
            onClicked: Radio.bandDown()
        }
        PanelKey {
            Layout.fillWidth: true
            label: "BAND ▲"; enabled: fp.live
            onClicked: Radio.bandUp()
        }
        PanelKey {
            id: bandKey
            Layout.fillWidth: true
            label: "BAND"
            sub: Radio.bandIndex >= 0 ? Radio.bands[Radio.bandIndex] : "—"
            enabled: fp.live
            onClicked: bandMenu.open()
            Menu {
                id: bandMenu
                y: bandKey.height
                Repeater {
                    model: Radio.bands
                    MenuItem {
                        required property string modelData
                        required property int index
                        text: modelData
                        onTriggered: Radio.selectBand(index)
                    }
                }
            }
        }
        PanelKey {
            id: modeKey
            Layout.fillWidth: true
            label: "MODE"
            sub: Radio.mode
            enabled: fp.live
            onClicked: modeMenu.open()
            // The families of the radio's MODE key. The radio picks the
            // sideband; the key's second line shows what it picked.
            Menu {
                id: modeMenu
                y: modeKey.height
                Repeater {
                    model: Radio.modes
                    MenuItem {
                        required property string modelData
                        text: modelData
                        checkable: true
                        checked: Radio.modeFamily === modelData
                        onTriggered: Radio.setMode(modelData)
                    }
                }
            }
        }

        PanelKey { Layout.fillWidth: true; label: "A/B";  enabled: fp.live; onClicked: Radio.run("A/B") }
        PanelKey { Layout.fillWidth: true; label: "A=B";  enabled: fp.live; onClicked: Radio.run("A=B") }
        PanelKey {
            Layout.fillWidth: true
            label: "V/M"
            sub: Radio.memMode === "VFO" ? "VFO" : "MEM " + Radio.memName
            lit: Radio.memMode !== "VFO"
            enabled: fp.live
            onClicked: Radio.run("VM")
        }
        PanelKey {
            Layout.fillWidth: true
            label: "SPLIT"; lit: Radio.split; enabled: fp.live
            onClicked: Radio.setParam("SPL", 0, Radio.split ? "0" : "1")
        }

        PanelKey {
            Layout.fillWidth: true
            label: "CLAR"; lit: fp.on("CLAR"); enabled: fp.live
            sub: (Radio.rxClar || Radio.txClar) ? ((Radio.clarOffset > 0 ? "+" : "") + Radio.clarOffset + " Hz") : ""
            onClicked: Radio.toggle("CLAR")
        }
        PanelKey { Layout.fillWidth: true; label: "CLEAR"; sub: "clarifier"; enabled: fp.live; onClicked: Radio.run("CLAR-CLR") }
        PanelKey { Layout.fillWidth: true; label: "LOCK"; lit: fp.on("LOCK"); lampColor: Ui.warn; enabled: fp.live; onClicked: Radio.toggle("LOCK") }
        PanelKey { Layout.fillWidth: true; label: "FAST"; lit: fp.on("FS"); enabled: fp.live; onClicked: Radio.toggle("FS") }

        PanelKey {
            Layout.fillWidth: true
            label: "TUNE"; sub: "ATU cycle"
            // Red while tuning; then lit as long as the tuner is on, as the
            // radio's own TUNE indicator.
            lit: Radio.tuning || fp.on("TNR")
            lampColor: Radio.tuning ? Ui.tx : Ui.accent
            enabled: fp.live && !Radio.ptt && !Radio.tuning && Radio.txAllowed
            onClicked: Radio.startTune()
        }
        PanelKey { Layout.fillWidth: true; label: "TNR"; lit: fp.on("TNR"); enabled: fp.live; onClicked: Radio.toggle("TNR") }
        PanelKey { Layout.fillWidth: true; label: "QMB"; sub: "store"; enabled: fp.live; onClicked: Radio.run("QMB-STO") }
        PanelKey { Layout.fillWidth: true; label: "QMB"; sub: "recall"; enabled: fp.live; onClicked: Radio.run("QMB-RCL") }
    }

    // -------------------------------------------------------- clarifier
    RowLayout {
        Layout.fillWidth: true
        visible: fp.on("CLAR") || Radio.rxClar || Radio.txClar
        spacing: 4
        Label { text: "CLAR"; color: Ui.accent; font.bold: true }
        Repeater {
            model: [-100, -10, 10, 100]
            Button {
                required property int modelData
                Layout.fillWidth: true
                text: (modelData > 0 ? "+" : "") + modelData
                enabled: fp.live
                autoRepeat: true
                onClicked: Radio.clarifier(modelData)
            }
        }
    }

    // ---------------------------------------------------- memory channel
    RowLayout {
        Layout.fillWidth: true
        visible: Radio.memMode !== "VFO"
        spacing: 6
        Label { text: qsTr("Memory"); color: Ui.dim }
        SpinBox {
            id: memBox
            from: 1; to: 99
            editable: true
            enabled: fp.live
            value: Math.max(1, Radio.memChannel)
            onValueModified: Radio.selectMemory(value)
        }
        Item { Layout.fillWidth: true }
    }

    // ---------------------------------------------------------- levels
    LevelSlider { Layout.fillWidth: true; code: "AF";  label: "AF" }
    LevelSlider { Layout.fillWidth: true; code: "RF";  label: "RF" }
    LevelSlider { Layout.fillWidth: true; code: "SQL"; label: "SQL" }
    LevelSlider { Layout.fillWidth: true; code: "MIC"; label: "MIC" }
    LevelSlider { Layout.fillWidth: true; code: "PWR"; label: "PWR" }
}
