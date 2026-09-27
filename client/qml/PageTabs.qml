// A strip of page names that scrolls sideways when the window is narrow.
// Qt's TabBar scrolls itself to keep the current tab away from the edges,
// which cut the first tab on a phone; this one only moves when asked.
import QtQuick
import QtQuick.Controls

Flickable {
    id: strip
    property var model: []
    property int currentIndex: 0
    implicitHeight: 44
    contentWidth: row.width
    contentHeight: height
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds
    clip: true

    Rectangle {
        anchors.bottom: parent.bottom
        width: Math.max(strip.width, row.width)
        height: 1
        color: Ui.edge
    }

    // The current tab is scrolled into view, whoever chose it.
    function showCurrent() {
        const b = tabs.itemAt(currentIndex)
        if (!b) return
        const maxX = Math.max(0, contentWidth - width)
        if (b.x < contentX) contentX = Math.max(0, b.x - 16)
        else if (b.x + b.width > contentX + width) contentX = Math.min(maxX, b.x + b.width - width + 16)
    }
    onCurrentIndexChanged: Qt.callLater(showCurrent)
    onWidthChanged: Qt.callLater(showCurrent)
    Component.onCompleted: Qt.callLater(showCurrent)

    Row {
        id: row
        height: strip.height
        Repeater {
            id: tabs
            model: strip.model
            AbstractButton {
                required property string modelData
                required property int index
                readonly property bool current: strip.currentIndex === index
                height: row.height
                width: Math.max(56, label.implicitWidth + 24)
                focusPolicy: Qt.NoFocus
                onClicked: strip.currentIndex = index
                contentItem: Text {
                    id: label
                    text: parent.modelData
                    color: parent.current ? Ui.accent : Ui.text
                    font.pixelSize: 13
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.down ? Ui.panelHi : "transparent"
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 3
                        color: Ui.accent
                        visible: parent.parent.current
                    }
                }
            }
        }
    }
}
