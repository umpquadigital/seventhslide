import QtQuick
import SeventhSlide

// A number in the sidebar, by its name in the property table: a NumberField
// that shows the value as it paints, holds it to the range C++ gives that
// name, and writes it back. ``from`` says whose the name is: the selection's
// (SlideObjectsSession.ranges), a theme setting's
// (SlideThemeSession.settingRanges), or a paragraph's (the caret's own while
// text is open, else the box's). ``percent`` shows a share (0 to 1) as a
// percentage; the field's numbers and its commit are then the percentage.
NumberField {
    id: field
    required property SlideEditState dialog
    required property string prop
    /// selection | setting | paragraph
    property string from: "selection"
    property bool percent: false

    readonly property int unitScale: field.percent ? 100 : 1
    readonly property var range: (field.from === "setting" ? field.dialog.settingRanges
                                                           : field.dialog.selRanges)[field.prop]
    readonly property real stored: {
        if (field.from === "setting")
            return Number(field.dialog.setting(field.prop))
        if (field.from === "paragraph")
            return Number(field.dialog.paragraphValue(field.prop, field.dialog.sel[field.prop]))
        return Number(field.dialog.sel[field.prop])
    }

    compact: true
    fieldWidth: 84
    minValue: field.range ? field.range.min * field.unitScale : null
    maxValue: field.range ? field.range.max * field.unitScale : null
    value: field.percent ? Math.round(field.stored * 100) : field.stored
    onCommit: function (n) {
        const v = n / field.unitScale
        if (field.from === "setting")
            field.dialog.setSetting(field.prop, v)
        else if (field.from === "paragraph")
            field.dialog.setParagraphProp(field.prop, v)
        else
            field.dialog.setProp(field.prop, v)
    }
}
