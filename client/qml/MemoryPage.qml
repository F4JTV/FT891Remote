// The radio's memories: 001-099 and the PMS pairs P1L-P9U, read with MR,
// written with MW, recalled with MC.
//
// Over CAT a memory holds its frequency, mode, clarifier, CTCSS on/off and
// repeater shift. Its CTCSS tone number and its name are not part of it:
// the FT-891 neither sends nor takes them with MR and MW.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

Item {
    id: page
    readonly property int rev: Radio.revision
    readonly property bool ready: Radio.connected && Radio.radioOn
    property bool showEmpty: true

    readonly property var toneNames: [qsTr("OFF"), qsTr("ENC/DEC"), qsTr("ENC")]
    readonly property var shiftNames: [qsTr("Simplex"), "+", "−"]

    // Read once, when the page is first shown with the radio on.
    function readIfNeeded() {
        if (visible && ready && Radio.memoriesRead === 0 && Radio.readTotal === 0) Radio.readMemories()
    }
    onVisibleChanged: readIfNeeded()
    onReadyChanged: readIfNeeded()
    Component.onCompleted: Qt.callLater(readIfNeeded)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Label {
                text: "MEMORY"
                color: Ui.accent
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                Layout.fillWidth: true
                text: Radio.memoriesRead > 0
                      ? qsTr("%1 used · %2 channels").arg(Radio.memoriesUsed).arg(Radio.memoryChannels.length)
                      : ""
                color: Ui.dim
                font.pixelSize: 12
                elide: Text.ElideRight
            }
            Button {
                text: qsTr("Read")
                enabled: page.ready && Radio.readTotal === 0
                onClicked: Radio.readMemories()
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            WrapCheck {
                text: qsTr("Show empty channels")
                checked: page.showEmpty
                onToggled: page.showEmpty = checked
            }
        }
        ProgressBar {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: Radio.readTotal > 0
            from: 0; to: Math.max(1, Radio.readTotal)
            value: Radio.readDone
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            // No spacing: it would add up between hidden rows. Each row
            // carries its own gap.
            spacing: 0
            cacheBuffer: 600
            // A fixed model: the rows read their channel, so that a value
            // arriving does not rebuild the list or move it.
            model: Radio.memoryChannels
            ScrollBar.vertical: ScrollBar { }

            delegate: Item {
                id: rowItem
                required property string modelData
                readonly property var m: page.rev >= 0 ? Radio.memory(modelData) : ({})
                readonly property bool shown: page.showEmpty || !(m.known && m.empty)
                width: ListView.view.width
                height: shown ? 50 : 0
                visible: shown

                Rectangle {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    anchors.bottomMargin: 4
                    radius: 6
                    color: tap.pressed ? Ui.panelHi : Ui.panel
                    border.color: Ui.edge
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.bottomMargin: 4
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 10
                    Text {
                        text: rowItem.modelData
                        color: Ui.accent
                        font.family: Ui.mono
                        font.pixelSize: 15
                        font.bold: true
                        Layout.preferredWidth: 40
                    }
                    Text {
                        Layout.fillWidth: true
                        text: !rowItem.m.known ? "…" : rowItem.m.empty ? qsTr("empty") : rowItem.m.freqText
                        color: rowItem.m.empty ? Ui.dim : Ui.lcdText
                        font.family: Ui.mono
                        font.pixelSize: 16
                        elide: Text.ElideRight
                    }
                    Text {
                        visible: !rowItem.m.empty
                        text: rowItem.m.mode || ""
                        color: Ui.text
                        font.bold: true
                        font.pixelSize: 13
                    }
                    Text {
                        // Clarifier, tone and shift, as short marks.
                        visible: !rowItem.m.empty
                        text: {
                            const f = []
                            if (rowItem.m.clarOn) f.push("CLAR " + (rowItem.m.clarOffset > 0 ? "+" : "") + rowItem.m.clarOffset)
                            if (rowItem.m.tone > 0) f.push(page.toneNames[rowItem.m.tone])
                            if (rowItem.m.shift > 0) f.push(page.shiftNames[rowItem.m.shift])
                            return f.join(" · ")
                        }
                        color: Ui.dim
                        font.pixelSize: 11
                    }
                }
                MouseArea {
                    id: tap
                    anchors.fill: parent
                    enabled: rowItem.m.known === true
                    onClicked: editor.openFor(rowItem.modelData)
                }
            }
        }
    }

    // ------------------------------------------------------------ editor
    Dialog {
        id: editor
        property string ch: ""
        property bool wasEmpty: true
        title: qsTr("Memory %1").arg(ch)
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(page.width - 24, 440)
        standardButtons: Dialog.Cancel

        function fill(m) {
            freqField.text = m.hz > 0 ? Radio.frequencyText(m.hz) : ""
            const i = Radio.exactModes.indexOf(m.mode)
            modeBox.currentIndex = i >= 0 ? i : Radio.exactModes.indexOf("USB")
            clarSwitch.checked = m.clarOn === true
            clarBox.value = m.clarOffset || 0
            toneBox.currentIndex = m.tone || 0
            shiftBox.currentIndex = m.shift || 0
            error.text = ""
        }
        function openFor(channel) {
            ch = channel
            const m = Radio.memory(channel)
            wasEmpty = m.empty === true
            fill(m.empty ? Radio.vfoAsMemory() : m)
            open()
        }
        function write() {
            const hz = Radio.frequencyFromText(freqField.text)
            if (hz <= 0) {
                error.text = qsTr("Not a frequency the FT-891 covers (30 kHz – 56 MHz)")
                return
            }
            const entry = {
                ch: ch, hz: hz, mode: modeBox.currentText,
                clarOn: clarSwitch.checked, clarOffset: clarBox.value,
                tone: toneBox.currentIndex, shift: shiftBox.currentIndex
            }
            const doWrite = function () { if (Radio.writeMemory(entry)) editor.close() }
            if (wasEmpty) doWrite()
            else Ui.ask(qsTr("Overwrite memory %1 on the radio?").arg(ch), doWrite)
        }

        ColumnLayout {
            width: parent.width
            spacing: 8

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 8
                Label { text: qsTr("Frequency"); color: Ui.dim }
                TextField {
                    id: freqField
                    Layout.fillWidth: true
                    font.family: Ui.mono
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    placeholderText: "14.074"
                }
                Label { text: qsTr("Mode"); color: Ui.dim }
                ComboBox {
                    id: modeBox
                    Layout.fillWidth: true
                    model: Radio.exactModes
                }
                Label { text: qsTr("Clarifier"); color: Ui.dim }
                RowLayout {
                    Layout.fillWidth: true
                    Switch { id: clarSwitch }
                    SpinBox {
                        id: clarBox
                        Layout.fillWidth: true
                        from: -9999; to: 9999; stepSize: 10
                        editable: true
                        enabled: clarSwitch.checked
                        textFromValue: function (v) { return (v > 0 ? "+" : "") + v + " Hz" }
                        valueFromText: function (t) { return parseInt(t) || 0 }
                    }
                }
                Label { text: qsTr("CTCSS"); color: Ui.dim }
                ComboBox {
                    id: toneBox
                    Layout.fillWidth: true
                    model: page.toneNames
                }
                Label { text: qsTr("Shift"); color: Ui.dim }
                ComboBox {
                    id: shiftBox
                    Layout.fillWidth: true
                    model: page.shiftNames
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("The CTCSS tone frequency and the memory's name are not part of a memory over CAT.")
                color: Ui.dim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label {
                id: error
                Layout.fillWidth: true
                visible: text.length > 0
                color: Ui.warn
                wrapMode: Text.WordWrap
            }

            // A row of buttons that moves to a second line on a phone.
            Item {
                Layout.fillWidth: true
                implicitHeight: actions.childrenRect.height
                Flow {
                    id: actions
                    width: parent.width
                    spacing: 6
                    Button {
                        text: qsTr("From VFO-A")
                        enabled: Radio.radioOn
                        onClicked: editor.fill(Radio.vfoAsMemory())
                    }
                    Button {
                        text: qsTr("Recall")
                        enabled: page.ready && !editor.wasEmpty
                        onClicked: { Radio.recallMemory(editor.ch); editor.close() }
                    }
                    Button {
                        text: qsTr("Write")
                        highlighted: true
                        enabled: page.ready
                        onClicked: editor.write()
                    }
                }
            }
        }
    }
}
