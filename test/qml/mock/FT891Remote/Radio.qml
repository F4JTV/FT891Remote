// Stand-in for the C++ bridge, for the QML tests: one text setting, and a
// record of what the page sends.
pragma Singleton
import QtQuick

QtObject {
    property int revision: 1
    property bool connected: true
    property bool radioOn: true
    property string mode: "CW-U"
    property string stored: "CQ TEST"
    property string lastSent: ""
    property int sends: 0

    function describe(c) {
        return { code: c, name: "Keyer memory 2 — text", canSet: true, canRead: true,
                 verified: false, confirm: "", note: "",
                 params: [ { name: "Text", type: "text", min: 0, max: 0, step: 1,
                             maxLength: 50, values: [] } ] }
    }
    function hasValue(c) { return true }
    function display(c) { return stored }
    function paramValue(c, i) { return stored }
    function paramInt(c, i) { return 0 }
    function enumIndex(c, i) { return -1 }
    function isOn(c) { return false }
    function describeStep(c, i, s) { return "" }
    function readCodes(c) {}
    // As the bridge does: the value is stored at once, before the read-back.
    function setParam(c, i, v, confirmed) { lastSent = v; sends++; stored = v; revision++ }
    // A value read from the radio.
    function radioReports(v) { stored = v; revision++ }
}
