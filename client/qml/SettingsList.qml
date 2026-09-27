// A page of settings: the rows of one or more groups of the description,
// with a button to read them all again from the radio.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

Item {
    id: page
    property string title: ""
    property var groups: []          // group names, in order
    property var extraCodes: []      // codes from other groups, appended
    property var hiddenCodes: []     // codes shown elsewhere on the panel
    property bool autoRead: true     // read what is missing when shown

    readonly property var codes: {
        let out = []
        for (let g = 0; g < groups.length; ++g) {
            const c = Radio.groupCodes(groups[g])
            for (let i = 0; i < c.length; ++i)
                if (hiddenCodes.indexOf(c[i]) < 0) out.push(c[i])
        }
        for (let j = 0; j < extraCodes.length; ++j) out.push(extraCodes[j])
        // A companion is shown in its switch's row, not in one of its own.
        return Radio.withoutCompanions(out)
    }

    function refresh() {
        for (let g = 0; g < groups.length; ++g) Radio.readGroup(groups[g])
        if (extraCodes.length > 0) Radio.readCodes(extraCodes)
    }

    // What is missing is read when the page is shown, and again when the
    // radio comes up while it is shown. A page shown first gets no visibility
    // change, hence the check at creation as well.
    readonly property bool ready: Radio.connected && Radio.radioOn
    function readMissing() {
        if (!visible || !autoRead || !ready) return
        for (let g = 0; g < groups.length; ++g)
            if (!Radio.groupComplete(groups[g])) Radio.readGroup(groups[g])
    }
    onVisibleChanged: readMissing()
    onReadyChanged: readMissing()
    Component.onCompleted: Qt.callLater(readMissing)

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            visible: page.title.length > 0
            Label {
                text: page.title
                color: Ui.accent
                font.bold: true
                font.pixelSize: 16
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Button {
                text: qsTr("Read")
                enabled: Radio.connected && Radio.radioOn
                onClicked: page.refresh()
            }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: page.codes
            cacheBuffer: 800
            ScrollBar.vertical: ScrollBar { }
            delegate: SettingRow {
                required property string modelData
                code: modelData
                width: ListView.view.width - 12
                x: 6
            }
        }
    }
}
