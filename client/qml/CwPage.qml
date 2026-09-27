// CW: a keyboard sent through the radio's keyer, macros, the five keyer
// memories of the radio, and the CW SETTING keys.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

Flickable {
    id: page
    clip: true
    contentHeight: col.implicitHeight + 16
    ScrollBar.vertical: ScrollBar { }
    readonly property bool live: Radio.connected && Radio.radioOn
    readonly property bool cwMode: Radio.mode.indexOf("CW") === 0

    ColumnLayout {
        id: col
        width: page.width - 16
        x: 8
        y: 8
        spacing: 8

        Label {
            Layout.fillWidth: true
            visible: page.live && !page.cwMode
            text: qsTr("The radio is in %1: its keyer only sends in CW.").arg(Radio.mode)
            color: Ui.warn
            wrapMode: Text.WordWrap
        }

        // ------------------------------------------------------ keyboard
        RowLayout {
            Layout.fillWidth: true
            TextField {
                id: cwText
                Layout.fillWidth: true
                placeholderText: qsTr("Text to send")
                font.capitalization: Font.AllUppercase
                enabled: page.live
                onAccepted: sendBtn.clicked()
            }
            Button {
                id: sendBtn
                text: qsTr("Send")
                highlighted: true
                enabled: page.live && cwText.text.length > 0 && !Radio.ptt
                onClicked: { Radio.sendCw(cwText.text); cwText.text = "" }
            }
            Button {
                text: qsTr("Stop")
                enabled: page.live
                onClicked: Radio.stopCw()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Speed"); color: Ui.dim }
            Slider {
                id: wpmSlider
                Layout.fillWidth: true
                from: 4; to: 60; stepSize: 1
                enabled: page.live
                value: Radio.wpm
                onPressedChanged: if (!pressed) Radio.wpm = Math.round(value)
            }
            Label { text: Math.round(wpmSlider.value) + " WPM"; font.family: Ui.mono; color: Ui.accent }
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("My call"); color: Ui.dim }
            TextField {
                Layout.fillWidth: true
                text: Radio.myCall
                placeholderText: qsTr("used by {MYCALL} in the macros")
                font.capitalization: Font.AllUppercase
                onEditingFinished: Radio.myCall = text
            }
        }

        // ---------------------------------------------------------- macros
        Label { text: qsTr("Macros — press to send, hold to edit"); color: Ui.accent; font.bold: true }
        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 4
            rowSpacing: 4
            Repeater {
                model: Radio.cwMacros
                PanelKey {
                    required property string modelData
                    required property int index
                    Layout.fillWidth: true
                    implicitWidth: 60
                    label: Radio.myCall.length >= 0 ? Radio.expandMacro(modelData) : modelData
                    enabled: page.live && !Radio.ptt
                    onClicked: Radio.sendCw(modelData)
                    onPressAndHold: {
                        macroEdit.index = index
                        macroEdit.text = modelData
                        macroDialog.open()
                    }
                    ToolTip.visible: hovered
                    ToolTip.text: label
                }
            }
        }

        // ------------------------------------------ the radio's memories
        Label { text: qsTr("Keyer memories of the radio"); color: Ui.accent; font.bold: true }
        Label {
            Layout.fillWidth: true
            text: qsTr("Stored in the FT-891 and played by it. A TEXT memory holds the text below; a MESSAGE memory was recorded with the paddle (menus 04-07 to 04-11). ▶ plays either. Free text uses memory 1, so its content is overwritten when you send typed text.")
            color: Ui.dim
            font.pixelSize: 11
            wrapMode: Text.WordWrap
        }
        // One ▶ per memory: the server plays it with the command of its type
        // (KY6-KYA for TEXT, KY1-KY5 for MESSAGE, as menus 04-07 to 04-11
        // set it).
        Repeater {
            model: 5
            RowLayout {
                id: memRow
                required property int index
                readonly property string typeCode: "EX04" + String(7 + index).padStart(2, "0")
                readonly property bool isMessage: page.rev >= 0 && Radio.paramValue(typeCode, 0) === "1"
                Layout.fillWidth: true
                SettingRow {
                    Layout.fillWidth: true
                    visible: !memRow.isMessage
                    code: "KM" + (memRow.index + 1)
                }
                Rectangle {
                    // A MESSAGE memory has no text to show: say what it is.
                    visible: memRow.isMessage
                    Layout.fillWidth: true
                    implicitHeight: 48
                    radius: 6
                    color: Ui.panel
                    border.color: Ui.edge
                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("Keyer memory %1 — MESSAGE, recorded with the paddle").arg(memRow.index + 1)
                        color: Ui.text
                        elide: Text.ElideRight
                    }
                }
                PanelKey {
                    glyph: "play"
                    label: String(memRow.index + 1)
                    sub: memRow.isMessage ? "MSG" : "TEXT"
                    implicitWidth: 64
                    enabled: page.live && !Radio.ptt
                    onClicked: Radio.playCwMemory(memRow.index + 1)
                }
            }
        }

        // ------------------------------------------------ CW SETTING keys
        Label { text: "CW SETTING"; color: Ui.accent; font.bold: true }
        Repeater {
            // APF and its frequency in one row, as on the FUNCTION pages.
            model: Radio.withoutCompanions(Radio.groupCodes("CW SETTING"))
            SettingRow {
                required property string modelData
                Layout.fillWidth: true
                code: modelData
            }
        }
    }

    Dialog {
        id: macroDialog
        title: qsTr("Edit macro")
        modal: true
        anchors.centerIn: Overlay.overlay
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            anchors.fill: parent
            TextField {
                id: macroEdit
                property int index: 0
                Layout.fillWidth: true
                Layout.preferredWidth: 320
                font.capitalization: Font.AllUppercase
            }
            Label { text: qsTr("{MYCALL} is replaced by your callsign."); color: Ui.dim; font.pixelSize: 11 }
        }
        onAccepted: Radio.setCwMacro(macroEdit.index, macroEdit.text)
    }

    readonly property int rev: Radio.revision
    function readMemories() {
        if (visible && live)
            Radio.readCodes(["KM1", "KM2", "KM3", "KM4", "KM5", "SPEED", "KEYER", "BK-IN",
                             "EX0407", "EX0408", "EX0409", "EX0410", "EX0411"])
    }
    onVisibleChanged: readMemories()
    onLiveChanged: readMemories()
    Component.onCompleted: Qt.callLater(readMemories)
}
