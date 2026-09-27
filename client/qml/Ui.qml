// Colours, sizes and a small message bus shared by every page.
pragma Singleton
import QtQuick

QtObject {
    // Front-panel palette: a dark body, an amber accent like the radio's own
    // key labels, and a blue-green LCD.
    readonly property color body:      "#1c222c"
    readonly property color panel:     "#252c38"
    readonly property color panelHi:   "#2f3847"
    readonly property color edge:      "#3a4454"
    readonly property color text:      "#e6ebf2"
    readonly property color dim:       "#8d97a8"
    readonly property color accent:    "#f0c674"
    readonly property color lcdBack:   "#0c1a1e"
    readonly property color lcdText:   "#78e6ff"
    readonly property color lcdDim:    "#2f5560"
    readonly property color tx:        "#ff5a4a"
    readonly property color ok:        "#6fdc8c"
    readonly property color warn:      "#ffb454"

    readonly property string mono: "monospace"

    // A question the operator must answer before a command leaves: a reset,
    // a CAT-rate change, switching the radio off. Main.qml shows the dialog.
    signal confirmRequested(string question, var action)
    function ask(question, action) { confirmRequested(question, action) }

    // Frequency entry, opened from the display.
    signal frequencyEntryRequested(bool vfoB)
}
