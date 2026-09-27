// Text settings (the CW keyer memories): what the operator types is what is
// sent, whether with the Set button or the Enter key.
//
//   qmltestrunner -import test/qml/mock -input test/qml/tst_settingrow_text.qml
import QtQuick
import QtQuick.Controls
import QtTest
import FT891Remote
import "../../client/qml"

Item {
    width: 500; height: 300
    SettingRow { id: row; code: "KM2"; width: 480 }

    TestCase {
        name: "SettingRowText"
        when: windowShown

        function find(item, test) {
            if (test(item)) return item
            for (let i = 0; i < item.children.length; ++i) {
                const f = find(item.children[i], test)
                if (f) return f
            }
            return null
        }
        function field() { return find(row, function (i) { return String(i).indexOf("TextField") === 0 }) }
        function setButton() { return find(row, function (i) { return i.text === "Set" && i.clicked !== undefined }) }

        function typeText(f, s) {
            mouseClick(f)
            f.selectAll()
            keyClick(Qt.Key_Delete)
            for (const ch of s) keyClick(ch === " " ? Qt.Key_Space : ch.charCodeAt(0))
        }

        function init() {
            Radio.radioReports("CQ TEST")
            Radio.lastSent = ""
            Radio.sends = 0
            field().focus = false
            wait(0)
        }

        function test_setButtonSendsTheTypedText() {
            const f = field()
            compare(f.text, "CQ TEST")
            typeText(f, "NEW TEXT")
            mouseClick(setButton())
            compare(Radio.sends, 1)
            compare(Radio.lastSent, "NEW TEXT")
            compare(f.text.toUpperCase(), "NEW TEXT", "the field shows what was sent")
        }

        function test_enterSendsTheTypedText() {
            const f = field()
            typeText(f, "DE N0CALL")
            keyClick(Qt.Key_Return)
            compare(Radio.lastSent, "DE N0CALL")
        }

        function test_aReadBackDoesNotOverwriteTyping() {
            const f = field()
            typeText(f, "HALF TYPED")
            Radio.radioReports("CQ TEST")      // the poll reads the memory
            compare(f.text.toUpperCase(), "HALF TYPED")
        }

        function test_anUntouchedFieldFollowsTheRadio() {
            Radio.radioReports("FROM RADIO")
            compare(field().text, "FROM RADIO")
        }

        function test_leavingWithoutSendingShowsTheRadioAgain() {
            const f = field()
            typeText(f, "NOT SENT")
            f.focus = false                   // the operator taps elsewhere
            compare(Radio.sends, 0, "nothing sent")
            compare(f.text, "CQ TEST", "the radio's value is back")
        }
    }
}
