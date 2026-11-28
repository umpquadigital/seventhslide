import QtQuick
import SeventhSlide

// An audio fade's length in a form (Fade in, Fade out, Overlap): seconds,
// with the + and - the slide editor's numbers have, a tenth of a second a
// step. ``ms`` is the fade as the form keeps it, in milliseconds ("" for none
// of its own, showing ``placeholder``); ``edited`` hands back the new one, ""
// when emptied. Without ``allowEmpty`` (an output's, which sets them all) it
// is never empty.
NumberField {
    id: fade

    property string ms: ""
    signal edited(string value)

    readonly property real parsed: parseInt(fade.ms, 10)

    compact: true
    height: Style.controlH
    fieldWidth: Style.fieldShortW
    unit: qsTr("s")
    displayScale: 0.001
    decimals: 3
    trimZeros: true
    step: 100
    minValue: 0
    maxValue: 60000
    live: true
    allowEmpty: true
    tabFocus: true
    empty: isNaN(fade.parsed)
    value: isNaN(fade.parsed) ? 0 : fade.parsed
    onCommit: function (n) { fade.edited(String(n)) }
    onCleared: fade.edited("")
}
