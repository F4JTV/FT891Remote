// The radio's MENU, 01 AGC to 18 VERSION: the list of sections, and each
// section's settings, read when it is opened.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FT891Remote

Item {
    id: page

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: sections
    }

    Component {
        id: sections
        ColumnLayout {
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 8
                Label {
                    text: "MENU"
                    color: Ui.accent
                    font.bold: true
                    font.pixelSize: 16
                    Layout.fillWidth: true
                }
                ProgressBar {
                    visible: Radio.readTotal > 0
                    from: 0; to: Math.max(1, Radio.readTotal)
                    value: Radio.readDone
                    Layout.preferredWidth: 120
                }
                Button {
                    text: qsTr("Read all")
                    enabled: Radio.connected && Radio.radioOn && Radio.readTotal === 0
                    onClicked: Radio.readAll()
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: Radio.menuGroups
                spacing: 4
                ScrollBar.vertical: ScrollBar { }
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: ListView.view.width
                    // "MENU — AGC" → "01  AGC"
                    text: String(index + 1).padStart(2, "0") + "   " + modelData.replace("MENU — ", "")
                    font.bold: true
                    onClicked: stack.push(section, { group: modelData })
                    background: Rectangle {
                        color: parent.down ? Ui.panelHi : Ui.panel
                        radius: 6
                        border.color: Ui.edge
                    }
                }
            }
        }
    }

    Component {
        id: section
        ColumnLayout {
            property string group
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 4
                ToolButton { text: "‹ " + qsTr("Menu"); onClicked: stack.pop() }
                Item { Layout.fillWidth: true }
            }
            SettingsList {
                Layout.fillWidth: true
                Layout.fillHeight: true
                title: group.replace("MENU — ", "")
                groups: [group]
            }
        }
    }
}
