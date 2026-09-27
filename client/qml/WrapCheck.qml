// A check box whose text wraps instead of widening the page.
//
// A CheckBox is as wide as its text on one line, and a ColumnLayout never
// lays out narrower than its widest child: one long option made the whole
// Setup page wider than a phone, and every field on it followed.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

CheckBox {
    id: box
    Layout.fillWidth: true
    contentItem: Text {
        text: box.text
        font: box.font
        color: box.enabled ? Ui.text : Ui.dim
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
        leftPadding: box.indicator ? box.indicator.width + box.spacing : 0
    }
}
