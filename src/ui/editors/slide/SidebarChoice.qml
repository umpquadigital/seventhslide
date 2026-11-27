import QtQuick
import SeventhSlide

// One of a fixed set of the selection's in the sidebar, by its name in the
// property table: a RibbonDropdown over ``options`` that shows the value and
// writes the one picked.
RibbonDropdown {
    id: choice
    required property SlideEditState dialog
    required property string prop

    compact: true
    fieldWidth: 110
    value: String(choice.dialog.sel[choice.prop])
    onPicked: function (v) { choice.dialog.setProp(choice.prop, v) }
}
