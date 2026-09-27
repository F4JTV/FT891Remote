import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

ColumnLayout {
    spacing: 6
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 8
        Label { text: qsTr("Log"); color: Ui.accent; font.bold: true; Layout.fillWidth: true }
        Button { text: qsTr("Clear"); onClicked: Radio.clearLog() }
    }
    LineLog {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 8
        text: Radio.logText
        textColor: Ui.text
        color: Ui.panel
        pixelSize: 12
    }
}
