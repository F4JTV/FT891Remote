// FT891Remote — the client window.
//
// Wide screens: the front panel on the left, the function pages on the
// right. Phones: the display on top, the PTT at the bottom, and the panel
// as the first of the pages in between.
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window
import FT891Remote

ApplicationWindow {
    id: win
    visible: true
    width: Radio.isAndroid ? 400 : 1180
    height: Radio.isAndroid ? 800 : 800
    minimumWidth: 360
    minimumHeight: 560
    title: "FT891Remote"
    color: Ui.body
    // Full screen on Android, as RemoteRig does. The activity hides the
    // system bars, but Qt shows them again for a window that is not full
    // screen: without this, the navigation bar stayed at the bottom.
    visibility: Radio.isAndroid ? Window.FullScreen : Window.AutomaticVisibility

    Material.theme: Material.Dark
    Material.accent: Ui.accent
    Material.primary: Ui.panel

    readonly property bool wide: width >= 940
    property int startTab: 0      // set by FT891_TAB, for screenshots

    // ------------------------------------------------------------ header
    // No room kept for the status bar: full screen, it is hidden. The room
    // it used to keep was an empty band above the header.
    header: ToolBar {
        background: Rectangle { color: Ui.panel }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 6
            spacing: 8
            Rectangle {
                width: 12; height: 12; radius: 6
                color: Radio.connected ? (Radio.radioOn ? Ui.ok : Ui.warn)
                                       : (Radio.retrying ? Ui.warn : Ui.tx)
            }
            Label {
                text: "FT891Remote"
                font.bold: true
                color: Ui.text
            }
            Label {
                Layout.fillWidth: true
                text: Radio.retrying ? Radio.retryText
                      : Radio.connected ? (Radio.radioOn ? Radio.host + " · " + Radio.rttMs + " ms"
                                                         : Radio.radioStatus)
                      : qsTr("Not connected")
                color: Ui.dim
                elide: Text.ElideRight
                font.pixelSize: 12
            }
            ToolButton {
                text: Radio.connected || Radio.retrying ? qsTr("Disconnect") : qsTr("Connect")
                onClicked: Radio.connected || Radio.retrying ? Radio.disconnectFromStation()
                                                             : Radio.connectToStation()
            }
        }
    }

    // ---------------------------------------------------------- content
    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 10

        // Left column on a wide screen; the whole window on a phone.
        ColumnLayout {
            Layout.fillHeight: true
            Layout.fillWidth: !win.wide
            Layout.preferredWidth: win.wide ? 520 : -1
            Layout.maximumWidth: win.wide ? 560 : 100000
            spacing: 8

            LcdDisplay {
                Layout.fillWidth: true
                compact: !win.wide && win.height < 760
            }

            // Wide: the panel sits under the display.
            Flickable {
                visible: win.wide
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: widePanel.implicitHeight
                ScrollBar.vertical: ScrollBar { }
                Loader {
                    id: widePanel
                    width: parent.width - 8
                    active: win.wide
                    sourceComponent: FrontPanel { }
                }
            }

            // Narrow: the pages sit under the display, panel first.
            Loader {
                active: !win.wide
                visible: active
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: Component { PagesView { withPanel: true } }
            }

            PttBar { Layout.fillWidth: true }
        }

        // Right column: the pages.
        Loader {
            active: win.wide
            visible: active
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: Component { PagesView { withPanel: false } }
        }
    }

    // ------------------------------------------------------------ pages
    // One instance at a time: on a phone the panel is the first page, on a
    // wide screen it has a column of its own.
    component PagesView: ColumnLayout {
        id: pv
        property bool withPanel: false
        spacing: 4
        PageTabs {
            id: tabs
            Layout.fillWidth: true
            currentIndex: win.startTab
            model: (pv.withPanel ? ["PANEL"] : []).concat(
                       ["F-1", "F-2", "CW", "FM", "MENU", "MEMORY", "SETUP", "LOG"])
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex + (pv.withPanel ? 0 : 1)

            Flickable {
                clip: true
                contentHeight: narrowPanel.implicitHeight + 8
                ScrollBar.vertical: ScrollBar { }
                Loader {
                    id: narrowPanel
                    width: parent.width - 8
                    active: pv.withPanel
                    sourceComponent: FrontPanel { }
                }
            }
            SettingsList { title: "FUNCTION-1"; groups: ["FUNCTION-1"]; extraCodes: ["VOX-G", "VOX-D"] }
            SettingsList { title: "FUNCTION-2"; groups: ["FUNCTION-2", "DVS"] }
            CwPage { }
            SettingsList { title: "FM SETTING"; groups: ["FM SETTING"] }
            MenuPage { }
            MemoryPage { }
            SetupPage { }
            LogPage { }
        }
    }

    // ------------------------------------------------------------ PTT
    component PttBar: RowLayout {
        spacing: 8
        Button {
            id: latch
            text: qsTr("Latch")
            checkable: true
            ToolTip.visible: hovered
            ToolTip.text: qsTr("PTT stays on until pressed again")
        }
        Rectangle {
            id: ptt
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            radius: 10
            readonly property bool active: Radio.pttLocal
            readonly property bool usable: Radio.connected && Radio.radioOn && Radio.txAllowed
                                           && !Radio.receiveOnly && !Radio.tuning && !Radio.cwBusy
            color: Radio.ptt ? Ui.tx : (active ? "#7a2a24" : Ui.panel)
            border.color: Radio.ptt ? "#ffb0a8" : Ui.edge
            border.width: 2
            opacity: usable || active ? 1.0 : 0.45
            Label {
                anchors.centerIn: parent
                text: Radio.ptt ? qsTr("TRANSMIT") : (!Radio.txAllowed && Radio.radioOn ? qsTr("OUT OF BAND") : "PTT")
                font.pixelSize: 22
                font.bold: true
                color: Ui.text
            }
            MouseArea {
                anchors.fill: parent
                enabled: ptt.usable || ptt.active
                onPressed: {
                    if (latch.checked) Radio.setPtt(!Radio.pttLocal)
                    else Radio.setPtt(true)
                }
                onReleased: if (!latch.checked) Radio.setPtt(false)
                onCanceled: if (!latch.checked) Radio.setPtt(false)
            }
        }
    }

    // ------------------------------------------------------ confirmations
    Dialog {
        id: confirmDialog
        property var action
        title: qsTr("Confirm")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 40, 420)
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            id: confirmText
            width: parent.width
            wrapMode: Text.WordWrap
        }
        onAccepted: if (action) action()
    }
    Connections {
        target: Ui
        function onConfirmRequested(question, action) {
            confirmText.text = question
            confirmDialog.action = action
            confirmDialog.open()
        }
        function onFrequencyEntryRequested(vfoB) {
            freqDialog.vfoB = vfoB
            freqField.text = ""
            freqDialog.open()
            freqField.forceActiveFocus()
        }
    }

    // --------------------------------------------------- frequency entry
    Dialog {
        id: freqDialog
        property bool vfoB: false
        title: vfoB ? qsTr("VFO-B frequency") : qsTr("Frequency")
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(win.width - 40, 360)
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: parent.width
            TextField {
                id: freqField
                Layout.fillWidth: true
                placeholderText: "14.074  ·  7074  ·  14074000"
                inputMethodHints: Qt.ImhFormattedNumbersOnly
                font.family: Ui.mono
                font.pixelSize: 20
                onAccepted: freqDialog.accept()
            }
            Label {
                id: freqError
                visible: false
                text: qsTr("Not a frequency the FT-891 covers (30 kHz – 56 MHz)")
                color: Ui.warn
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        onAccepted: {
            if (!Radio.enterFrequency(freqField.text, vfoB)) {
                freqError.visible = true
                open()
            } else {
                freqError.visible = false
            }
        }
    }

    // ------------------------------------------------------------ notices
    Rectangle {
        id: toast
        property alias text: toastText.text
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 96
        width: Math.min(win.width - 40, toastText.implicitWidth + 32)
        height: toastText.implicitHeight + 20
        radius: 8
        color: "#e0303a48"
        border.color: Ui.accent
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Label {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(win.width - 72, implicitWidth)
            wrapMode: Text.WordWrap
            color: Ui.text
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.opacity = 0 }
    }
    Connections {
        target: Radio
        function onNoticeRaised(text) {
            toast.text = text
            toast.opacity = 1
            toastTimer.restart()
        }
    }

    Component.onCompleted: {
        if (Radio.host.length > 0 && Radio.autoReconnect && Radio.password.length > 0)
            Radio.connectToStation()
    }
}
