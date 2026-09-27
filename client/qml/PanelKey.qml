// A key of the front panel: a label or an icon, an optional second line, and
// a lamp that shows the function is on.
//
// Every key has the same height, whether it has a second line or not: in a
// row of keys, one taller than the others looked like a mistake.
import QtQuick
import QtQuick.Controls

AbstractButton {
    id: key
    property string label: ""
    property string sub: ""
    property string glyph: ""         // an Icon name, drawn instead of the label
    property bool lit: false
    property color lampColor: Ui.accent
    readonly property color inkColor: enabled ? Ui.text : Ui.dim
    implicitWidth: 72
    implicitHeight: 48
    focusPolicy: Qt.NoFocus
    hoverEnabled: true

    background: Rectangle {
        radius: 6
        color: key.down ? Ui.edge : (key.hovered ? Ui.panelHi : Ui.panel)
        border.color: key.lit ? key.lampColor : Ui.edge
        border.width: key.lit ? 2 : 1
        opacity: key.enabled ? 1.0 : 0.4
        Rectangle {
            width: 16; height: 3; radius: 1.5
            anchors.top: parent.top; anchors.topMargin: 4
            anchors.horizontalCenter: parent.horizontalCenter
            color: key.lit ? key.lampColor : Ui.edge
        }
    }

    contentItem: Item {
        // The lamp takes the top few pixels: the text is centred below it.
        Column {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: 2
            width: parent.width
            spacing: 0
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 4
                visible: key.glyph.length > 0
                Icon {
                    name: key.glyph
                    color: key.inkColor
                    width: 16; height: 16
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    visible: key.label.length > 0
                    text: key.label
                    color: key.inkColor
                    font.pixelSize: 13
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Text {
                visible: key.glyph.length === 0
                width: parent.width
                text: key.label
                color: key.inkColor
                font.pixelSize: 13
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
            Text {
                visible: key.sub.length > 0
                width: parent.width
                text: key.sub
                color: Ui.accent
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }
    }
}
