// Connection, audio and application settings.
//
// Rule for this page: nothing in the column may be wider than a phone. A
// ColumnLayout never lays out narrower than its widest child, so a single
// item with a fixed width — a long check box, a row of buttons, a label
// column of fixed size — made every field stretch past the right edge.
// Labels take their own width, fields and lists fill what remains, long
// texts wrap, and rows of buttons flow onto a second line.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

Flickable {
    id: page
    clip: true
    contentHeight: col.implicitHeight + 24
    ScrollBar.vertical: ScrollBar { }

    component Section: Label {
        color: Ui.accent
        font.bold: true
        font.pixelSize: 15
        Layout.topMargin: 10
    }
    component Field: Label {
        color: Ui.dim
        Layout.alignment: Qt.AlignVCenter
    }
    // A row of buttons that moves to a second line when the page is narrow.
    // A Flow does not report its second row to a layout: the Item does.
    component ButtonFlow: Item {
        default property alias buttons: flow.data
        Layout.fillWidth: true
        implicitHeight: flow.childrenRect.height
        Flow {
            id: flow
            width: parent.width
            spacing: 8
        }
    }
    function indexOfDevice(list, id) {
        for (let i = 0; i < list.length; ++i) if (list[i].id === id) return i
        return -1
    }

    ColumnLayout {
        id: col
        width: page.width - 32
        x: 16
        y: 8
        spacing: 6

        // ---------------------------------------------------------- station
        Section { text: qsTr("Station") }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            Field { text: qsTr("Address") }
            TextField {
                Layout.fillWidth: true
                text: Radio.host
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhUrlCharactersOnly
                onEditingFinished: Radio.host = text
            }
            Field { text: qsTr("Control port") }
            SpinBox {
                Layout.fillWidth: true
                Layout.maximumWidth: 240
                from: 1; to: 65535; editable: true
                value: Radio.port
                onValueModified: Radio.port = value
                textFromValue: function (v) { return String(v) }
            }
            Field { text: qsTr("Audio port") }
            SpinBox {
                Layout.fillWidth: true
                Layout.maximumWidth: 240
                from: 0; to: 65535; editable: true
                value: Radio.udpPort
                onValueModified: Radio.udpPort = value
                textFromValue: function (v) { return v === 0 ? qsTr("announced") : String(v) }
            }
            Field { text: qsTr("Password") }
            TextField {
                Layout.fillWidth: true
                text: Radio.password
                echoMode: TextInput.Password
                onEditingFinished: Radio.password = text
            }
        }
        WrapCheck { text: qsTr("Encrypt the link"); checked: Radio.encrypt; onToggled: Radio.encrypt = checked }
        WrapCheck { text: qsTr("Reconnect automatically"); checked: Radio.autoReconnect; onToggled: Radio.autoReconnect = checked }
        RowLayout {
            Layout.fillWidth: true
            Button {
                text: Radio.connected || Radio.retrying ? qsTr("Disconnect") : qsTr("Connect")
                highlighted: !Radio.connected
                onClicked: Radio.connected || Radio.retrying ? Radio.disconnectFromStation()
                                                             : Radio.connectToStation()
            }
            Label {
                text: Radio.retrying ? Radio.retryText : Radio.statusText
                color: Radio.connected ? Ui.ok : Ui.dim
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        Label {
            visible: Radio.connected
            text: qsTr("Server %1 · IARU region %2 · round trip %3 ms · lost %4 · buffer %5 ms")
                  .arg(Radio.serverVersion).arg(Radio.region).arg(Radio.rttMs)
                  .arg(Radio.lostFrames).arg(Radio.jitterMs)
            color: Ui.dim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // ------------------------------------------------------------ audio
        Section { text: qsTr("Audio") }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 8
            Field { text: qsTr("Speaker") }
            ComboBox {
                Layout.fillWidth: true
                model: Radio.outputDevices
                textRole: "name"
                currentIndex: page.indexOfDevice(Radio.outputDevices, Radio.outputDevice)
                onActivated: (i) => Radio.outputDevice = Radio.outputDevices[i].id
            }
            Field { text: qsTr("Microphone") }
            ComboBox {
                Layout.fillWidth: true
                model: [{ id: -1, name: qsTr("None — receive only") }].concat(Radio.inputDevices)
                textRole: "name"
                currentIndex: Radio.inputDevice < 0 ? 0 : page.indexOfDevice(Radio.inputDevices, Radio.inputDevice) + 1
                onActivated: (i) => Radio.inputDevice = (i === 0 ? -1 : Radio.inputDevices[i - 1].id)
            }
            Field { text: qsTr("Codec") }
            ComboBox {
                Layout.fillWidth: true
                model: ["Opus", "PCM 16-bit"]
                currentIndex: Radio.codec === "opus" ? 0 : 1
                onActivated: (i) => Radio.codec = (i === 0 ? "opus" : "pcm")
            }
            Field { text: qsTr("Jitter buffer") }
            RowLayout {
                Layout.fillWidth: true
                Slider {
                    id: jit
                    Layout.fillWidth: true
                    from: 20; to: 300; stepSize: 10
                    value: Radio.jitterTarget
                    onPressedChanged: if (!pressed) Radio.jitterTarget = value
                }
                Label { text: Math.round(jit.value) + " ms"; font.family: Ui.mono }
            }
            Field { text: qsTr("Receive gain") }
            RowLayout {
                Layout.fillWidth: true
                Slider {
                    id: rxg
                    Layout.fillWidth: true
                    from: 0.1; to: 4.0; stepSize: 0.1
                    value: Radio.rxGain
                    onMoved: Radio.rxGain = value
                }
                Label { text: rxg.value.toFixed(1); font.family: Ui.mono }
            }
            Field { text: qsTr("Transmit gain") }
            RowLayout {
                Layout.fillWidth: true
                Slider {
                    id: txg
                    Layout.fillWidth: true
                    from: 0.1; to: 4.0; stepSize: 0.1
                    value: Radio.txGain
                    onMoved: Radio.txGain = value
                }
                Label { text: txg.value.toFixed(1); font.family: Ui.mono }
            }
            Field { text: qsTr("Voice shaping") }
            ComboBox {
                Layout.fillWidth: true
                model: [qsTr("None — use the radio's PROC and EQ"), qsTr("Boom headset"), qsTr("Phone microphone")]
                currentIndex: Radio.speechPreset
                onActivated: (i) => Radio.speechPreset = i
            }
        }
        ButtonFlow {
            Button { text: qsTr("Look for audio devices again"); onClicked: Radio.refreshDevices() }
        }

        // ---------------------------------------------------------- controls
        Section { text: qsTr("Controls") }
        WrapCheck {
            visible: Radio.isAndroid
            text: qsTr("PTT on the volume-down key")
            checked: Radio.pttOnVolumeKey
            onToggled: Radio.pttOnVolumeKey = checked
        }
        WrapCheck {
            visible: !Radio.isAndroid
            text: qsTr("rigctld interface on 127.0.0.1:%1 (WSJT-X, fldigi…)").arg(Radio.rigctldPort)
            checked: Radio.rigctldEnabled
            onToggled: Radio.rigctldEnabled = checked
        }
        Label {
            visible: !Radio.isAndroid
            text: qsTr("Space bar: push to talk, when no text field has the focus. Mouse wheel on the frequency: tuning, Shift for ten steps.")
            color: Ui.dim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // ------------------------------------------------------------- radio
        Section { text: qsTr("Radio") }
        ButtonFlow {
            Button {
                text: qsTr("Switch on")
                enabled: Radio.connected && Radio.linkOpen && !Radio.radioOn
                onClicked: Radio.power(true)
            }
            Button {
                text: qsTr("Switch off")
                enabled: Radio.connected && Radio.radioOn
                onClicked: Ui.ask(qsTr("Switch the radio off? It can only be switched back on over CAT if its USB port stays powered."),
                                  function () { Radio.power(false) })
            }
            Button {
                text: qsTr("Read every setting")
                enabled: Radio.connected && Radio.radioOn && Radio.readTotal === 0
                onClicked: Radio.readAll()
            }
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: Radio.readTotal > 0
            from: 0; to: Math.max(1, Radio.readTotal)
            value: Radio.readDone
        }

        Label {
            Layout.topMargin: 16
            text: qsTr("FT891Remote %1 — MIT licence").arg(Radio.version)
            color: Ui.dim
            font.pixelSize: 12
        }
    }
}
