// A segmented bar graph, like the radio's own meter.
import QtQuick
import QtQuick.Layouts

Item {
    id: bar
    property real value: 0          // 0 … 1
    property string label: ""
    property string valueText: ""
    property int segments: 30
    property real redFrom: 0.62     // fraction where the bar turns red
    property color onColor: Ui.lcdText
    property color hotColor: Ui.tx
    implicitHeight: 22
    implicitWidth: 240

    RowLayout {
        anchors.fill: parent
        spacing: 6
        Text {
            text: bar.label
            color: Ui.lcdText
            font.family: Ui.mono
            font.pixelSize: 12
            Layout.preferredWidth: 36
        }
        Row {
            id: row
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 2
            readonly property real segW: (width - spacing * (bar.segments - 1)) / bar.segments
            Repeater {
                model: bar.segments
                Rectangle {
                    width: row.segW
                    height: row.height
                    radius: 1
                    readonly property real at: (index + 1) / bar.segments
                    color: bar.value >= at - 0.5 / bar.segments
                           ? (at > bar.redFrom ? bar.hotColor : bar.onColor)
                           : Ui.lcdDim
                    opacity: bar.value >= at - 0.5 / bar.segments ? 1.0 : 0.35
                }
            }
        }
        Text {
            text: bar.valueText
            color: Ui.lcdText
            font.family: Ui.mono
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: 56
        }
    }
}
