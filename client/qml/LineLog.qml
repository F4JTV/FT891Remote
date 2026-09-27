// A scrolling list of lines: the log.
//
// A read-only TextArea in a ScrollView was used first. Keeping its last line
// in view meant moving its cursor or its content position, and either one
// pushed a short text to the bottom of the view. A ListView of lines has no
// cursor and scrolls to its end directly.
import QtQuick
import QtQuick.Controls

Rectangle {
    id: box
    property string text: ""
    property color textColor: Ui.lcdText
    property int pixelSize: 14
    color: Ui.lcdBack
    radius: 6

    ListView {
        id: lines
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        model: box.text.length > 0 ? box.text.split("\n") : []
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { }
        delegate: Text {
            required property string modelData
            width: ListView.view.width - 10
            text: modelData
            color: box.textColor
            font.family: Ui.mono
            font.pixelSize: box.pixelSize
            wrapMode: Text.WrapAnywhere
        }
        onCountChanged: Qt.callLater(positionViewAtEnd)
    }
}
