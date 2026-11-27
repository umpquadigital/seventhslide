import QtQuick
import SeventhSlide

// A colour of the selection's in the sidebar, by its name in the property
// table: a RibbonSwatch that shows it and writes the one picked.
RibbonSwatch {
    id: swatch
    required property SlideEditState dialog
    required property string prop

    compact: true
    value: String(swatch.dialog.sel[swatch.prop] || "")
    onEdited: function (v) { swatch.dialog.setProp(swatch.prop, v) }
}
