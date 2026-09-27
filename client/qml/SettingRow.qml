// One setting of the radio, drawn from its description in ft891.json: a
// switch for OFF/ON, segments or a list for a choice, a slider for a range,
// a text field for a keyer memory, a button for an action.
//
// Nothing here knows about any particular setting: adding one to the JSON
// is enough for it to appear, with the right control.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

Rectangle {
    id: row
    property string code
    readonly property var d: Radio.describe(code)
    readonly property var params: d.params || []
    readonly property int rev: Radio.revision
    readonly property bool known: rev >= 0 && Radio.hasValue(code)
    readonly property bool writable: d.canSet === true && Radio.connected && Radio.radioOn
    // A single OFF/ON switch sits in the title line: no second line for it.
    readonly property bool singleSwitch: params.length === 1 && isOffOn(params[0]) && d.canRead === true
    // An ON/OFF first parameter followed by a value — IF SHIFT, WIDTH: the
    // switch goes in the title line, the value below.
    readonly property bool leadSwitch: params.length > 1 && isOffOn(params[0]) && d.canRead === true
    readonly property bool headSwitchShown: singleSwitch || leadSwitch
    // Read-only information without parameters: the versions of menu 18.
    readonly property bool infoOnly: params.length === 0 && d.canRead === true && d.canSet !== true
    readonly property bool showsValue: d.canRead === true && (params.length > 0 || infoOnly)
    // The setting of the function this switch turns on — PRC and its level —
    // shown in this row rather than in a row of its own.
    readonly property string companion: d.companion || ""
    // Drawn inside its switch's row: no frame, no title, the control only.
    property bool embedded: false

    implicitHeight: body.implicitHeight + (embedded ? 0 : 16)
    color: embedded ? "transparent" : Ui.panel
    radius: 6
    border.color: embedded ? "transparent" : Ui.edge
    border.width: embedded ? 0 : 1

    // Every write goes through here, so that a command the description marks
    // with a question is asked first.
    function send(fn) {
        if (d.confirm && d.confirm.length > 0)
            Ui.ask(d.confirm, function () { fn(true) })
        else
            fn(false)
    }

    function isOffOn(p) {
        return p.type === "enum" && p.values.length === 2
               && p.values[0].label === "OFF" && p.values[1].label === "ON"
    }
    function segmented(p) {
        if (p.type !== "enum" || p.values.length > 4) return false
        let n = 0
        for (let i = 0; i < p.values.length; ++i) n += p.values[i].label.length
        return n <= 28
    }

    ColumnLayout {
        id: body
        anchors.fill: parent
        anchors.margins: row.embedded ? 0 : 8
        spacing: 6

        RowLayout {
            visible: !row.embedded
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: row.d.name || row.code
                color: Ui.text
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            // Sized through an implicit width with a fixed cap. A size hint
            // tied to the row's width was computed while the row was still
            // being laid out (73 px), and Qt 6.4's RowLayout did not apply the
            // corrected hint once the row had its real width: values stayed
            // cut short. Text's own implicit width is read-only, hence the Item.
            Item {
                visible: (row.showsValue && !row.singleSwitch) || row.companion.length > 0
                         && (row.infoOnly || row.params[0].type !== "text")   // text: own field
                implicitWidth: Math.min(Math.ceil(valueMetrics.advanceWidth) + 12, 200)
                implicitHeight: valueText.implicitHeight
                Text {
                    id: valueText
                    anchors.fill: parent
                    // Also re-read on a mode change: the same WIDTH step is
                    // another bandwidth in another mode.
                    text: row.rev >= 0 && Radio.mode.length >= 0
                          ? (row.companion.length > 0 ? Radio.describeCurrent(row.companion, 0)
                             : !row.known ? "—"
                             : row.leadSwitch ? Radio.describeCurrent(row.code, 1) : Radio.display(row.code)) : "—"
                    color: Ui.accent
                    font.family: Ui.mono
                    font.pixelSize: 14
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignRight
                }
                TextMetrics { id: valueMetrics; font: valueText.font; text: valueText.text }
            }
            Switch {
                id: headSwitch
                visible: row.headSwitchShown
                enabled: row.writable
                onToggled: {
                    const on = checked
                    row.send(function (c) { Radio.setSwitch(row.code, 0, on, c) })
                }
                Binding {
                    target: headSwitch; property: "checked"
                    value: row.rev >= 0 && Radio.isOn(row.code)
                    when: !headSwitch.pressed
                    restoreMode: Binding.RestoreNone
                }
            }
            ToolButton {
                id: rereadButton
                visible: row.showsValue
                implicitWidth: 36
                implicitHeight: 32
                enabled: Radio.connected
                // Drawn, not a font glyph: Android has no « ⟳ » character.
                contentItem: Icon {
                    name: "refresh"
                    color: rereadButton.enabled ? Ui.dim : Ui.edge
                    width: 18; height: 18
                }
                onClicked: Radio.readCodes(row.companion.length > 0 ? [row.code, row.companion] : [row.code])
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Read again from the radio")
            }
        }

        Label {
            visible: !row.embedded && (row.d.note || "").length > 0
            text: row.d.note || ""
            color: Ui.dim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // An action: one button.
        Button {
            visible: row.params.length === 0 && row.d.canSet === true
            text: row.d.name || row.code
            enabled: row.writable
            Layout.fillWidth: true
            onClicked: row.send(function (c) { Radio.run(row.code, c) })
        }

        Repeater {
            // The switch of a lead-switch command is in the title line: its
            // editors start with the value.
            model: row.singleSwitch ? [] : row.leadSwitch ? row.params.slice(1) : row.params
            delegate: Loader {
                id: editor
                required property var modelData
                required property int index
                Layout.fillWidth: true
                readonly property var p: modelData
                readonly property int pi: row.leadSwitch ? index + 1 : index
                sourceComponent: {
                    if (p.type === "text") return textEditor
                    if (p.type === "range") return rangeEditor
                    if (row.isOffOn(p)) return switchEditor
                    if (row.d.canRead !== true) return buttonsEditor
                    if (row.segmented(p)) return segmentEditor
                    return comboEditor
                }

                // ------------------------------------------ OFF / ON
                Component {
                    id: switchEditor
                    RowLayout {
                        Label {
                            text: editor.p.name
                            color: Ui.dim
                            visible: row.params.length > 1
                        }
                        Item { Layout.fillWidth: true }
                        Switch {
                            id: sw
                            enabled: row.writable
                            onToggled: {
                                const on = checked
                                row.send(function (c) { Radio.setSwitch(row.code, editor.pi, on, c) })
                            }
                            Binding {
                                target: sw; property: "checked"
                                value: row.rev >= 0 && Radio.isOnAt(row.code, editor.pi)
                                when: !sw.pressed
                                restoreMode: Binding.RestoreNone
                            }
                        }
                    }
                }

                // ------------------------------------- a few choices
                Component {
                    id: segmentEditor
                    // A Flow does not report its second row to the layout it
                    // sits in: the wrapper does.
                    Item {
                        implicitHeight: keys.childrenRect.height
                        Flow {
                            id: keys
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: editor.p.values
                                PanelKey {
                                    required property var modelData
                                    required property int index
                                    label: modelData.label
                                    lit: row.rev >= 0 && Radio.enumIndex(row.code, editor.pi) === index
                                    enabled: row.writable
                                    implicitWidth: Math.max(64, labelWidth.advanceWidth + 24)
                                    TextMetrics { id: labelWidth; text: modelData.label; font.pixelSize: 13; font.bold: true }
                                    onClicked: {
                                        const i = index
                                        row.send(function (c) { Radio.setEnumIndex(row.code, editor.pi, i, c) })
                                    }
                                }
                            }
                        }
                    }
                }

                // ------------------------------------- write-only
                Component {
                    id: buttonsEditor
                    // A Flow does not report its second row to the layout it
                    // sits in: the wrapper does.
                    Item {
                        implicitHeight: keys.childrenRect.height
                        Flow {
                            id: keys
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: editor.p.values
                                PanelKey {
                                    required property var modelData
                                    required property int index
                                    label: modelData.label
                                    enabled: row.writable
                                    implicitWidth: Math.max(64, labelWidth2.advanceWidth + 24)
                                    TextMetrics { id: labelWidth2; text: modelData.label; font.pixelSize: 13; font.bold: true }
                                    onClicked: {
                                        const i = index
                                        row.send(function (c) { Radio.setEnumIndex(row.code, editor.pi, i, c) })
                                    }
                                }
                            }
                        }
                    }
                }

                // ------------------------------------- a long list
                Component {
                    id: comboEditor
                    ComboBox {
                        id: combo
                        enabled: row.writable
                        model: editor.p.values
                        textRole: "label"
                        onActivated: (i) => row.send(function (c) { Radio.setEnumIndex(row.code, editor.pi, i, c) })
                        Binding {
                            target: combo; property: "currentIndex"
                            value: row.rev >= 0 ? Radio.enumIndex(row.code, editor.pi) : -1
                            when: !combo.popup.visible
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                // ---------------------------------------- a range
                Component {
                    id: rangeEditor
                    ColumnLayout {
                        spacing: 2
                        // The range offered now. Static for most settings;
                        // WIDTH follows the mode and NARROW, and has none in
                        // AM and FM. Read again when either changes.
                        readonly property var rng: row.rev >= 0 && Radio.mode.length >= 0
                            ? Radio.paramRange(row.code, editor.pi)
                            : ({ min: editor.p.min, max: editor.p.max, step: editor.p.step, available: true })
                        readonly property bool usable: row.writable && rng.available === true
                        Label {
                            visible: parent.rng.available === false
                            text: parent.rng.reason || ""
                            color: Ui.dim
                            font.pixelSize: 12
                        }
                        RowLayout {
                            id: rangeRow
                            visible: parent.rng.available !== false
                            spacing: 4
                            readonly property var rng: parent.rng
                            Label {
                                text: editor.p.name
                                color: Ui.dim
                                visible: row.params.length > 1 || row.embedded
                            }
                            PanelKey {
                                label: "−"
                                autoRepeat: true
                                enabled: rangeRow.parent.usable && slider.value > rangeRow.rng.min
                                implicitWidth: 40
                                onClicked: {
                                    const v = Math.max(rangeRow.rng.min, Math.round(slider.value) - rangeRow.rng.step)
                                    Radio.setParam(row.code, editor.pi, String(v))
                                }
                            }
                            Slider {
                                id: slider
                                Layout.fillWidth: true
                                enabled: rangeRow.parent.usable
                                from: rangeRow.rng.min
                                to: rangeRow.rng.max
                                stepSize: rangeRow.rng.step
                                snapMode: Slider.SnapAlways
                                live: true
                                onPressedChanged: {
                                    if (!pressed) {
                                        const v = String(Math.round(value))
                                        row.send(function (c) { Radio.setParam(row.code, editor.pi, v, c) })
                                    }
                                }
                                Binding {
                                    target: slider; property: "value"
                                    value: row.rev >= 0 && Radio.mode.length >= 0
                                           ? Radio.paramPosition(row.code, editor.pi) : rangeRow.rng.min
                                    when: !slider.pressed
                                    restoreMode: Binding.RestoreNone
                                }
                            }
                            PanelKey {
                                label: "+"
                                autoRepeat: true
                                enabled: rangeRow.parent.usable && slider.value < rangeRow.rng.max
                                implicitWidth: 40
                                onClicked: {
                                    const v = Math.min(rangeRow.rng.max, Math.round(slider.value) + rangeRow.rng.step)
                                    Radio.setParam(row.code, editor.pi, String(v))
                                }
                            }
                            Label {
                                // Dragging: the step being chosen. At rest: the
                                // radio's value, which the slider may not be
                                // able to stand on.
                                text: row.rev >= 0 && Radio.mode.length >= 0
                                      ? (slider.pressed ? Radio.describeStep(row.code, editor.pi, Math.round(slider.value))
                                                        : Radio.describeCurrent(row.code, editor.pi))
                                      : ""
                                color: slider.pressed ? Ui.accent : Ui.text
                                font.family: Ui.mono
                                horizontalAlignment: Text.AlignRight
                                // Its own width, "2400 Hz (default)" included;
                                // at least that of a short value, so that the
                                // slider does not jump as the text changes.
                                Layout.minimumWidth: 96
                            }
                        }
                    }
                }

                // ----------------------------------------- text
                Component {
                    id: textEditor
                    RowLayout {
                        // Set or Enter sends what was typed; leaving the
                        // field without sending shows the radio's value again.
                        TextField {
                            id: field
                            Layout.fillWidth: true
                            enabled: row.writable
                            maximumLength: editor.p.maxLength
                            font.capitalization: Font.AllUppercase
                            placeholderText: qsTr("A–Z 0–9 / ? . , = + -")
                            // The radio's value, while the operator is not typing.
                            Binding {
                                target: field; property: "text"
                                value: row.rev >= 0 ? Radio.paramValue(row.code, editor.pi).trim() : ""
                                when: !field.activeFocus
                                restoreMode: Binding.RestoreNone
                            }
                            // The text is read before the field lets go of the
                            // focus: from then on the binding owns it again.
                            function send() {
                                const t = text.toUpperCase()
                                row.send(function (c) { Radio.setParam(row.code, editor.pi, t, c) })
                                focus = false
                            }
                            onAccepted: send()
                        }
                        Button {
                            id: setBtn
                            text: qsTr("Set")
                            enabled: row.writable
                            // Without this, pressing Set took the focus from
                            // the field, the binding put the radio's old text
                            // back, and Set then sent the old text.
                            focusPolicy: Qt.NoFocus
                            onClicked: field.send()
                        }
                    }
                }
            }
        }

        // The companion setting, under the switch: its control only.
        Loader {
            id: companionLoader
            Layout.fillWidth: true
            active: row.companion.length > 0 && !row.embedded
            visible: active
            Component.onCompleted: {
                if (active) setSource("SettingRow.qml", { code: row.companion, embedded: true })
            }
        }
    }
}
