pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// One of a lyrics box's line types under follow-lines: the active line's or
// the other lines'. Each field is the line's own or, left to the box, the
// box's; the controls show what the line is painted in and write the line's
// own. The font's "Same as box" hands the family back.
SidebarSection {
    id: sec
    required property var side
    /// ``active_line`` | ``other_lines``: the properties' prefix.
    required property string line
    readonly property var sel: sec.side.sel
    readonly property string tag: sec.line === "active_line" ? "Active" : "Others"
    /// The span the line's own size is held to (0 hands it back to the box).
    readonly property var sizeRange: sec.side.dialog.selRanges[sec.line + "_size"]

    /// The line's own value of ``field`` (empty, 0 or null: the box's).
    function own(field) { return sec.sel[sec.line + "_" + field] }
    /// A flag as painted: the line's where it has one, else the box's.
    function flag(field) {
        const v = sec.own(field)
        return v === null || v === undefined ? !!sec.sel[field] : !!v
    }

    dialog: sec.side.dialog
    sectionId: sec.line

    // The family and the size on one row, the B I U and the colour below.
    SidebarRow {
        wide: true
        label: ""
        RibbonDropdown {
            compact: true
            objectName: "side" + sec.tag + "Font"
            fieldWidth: sec.width - 20 - 66 - 4
            options: [{ value: "", label: qsTr("Same as box") }].concat(
                         sec.side.session ? sec.side.session.fonts.families : [])
            value: String(sec.own("family") || "")
            onPicked: function (v) { sec.side.setProp(sec.line + "_family", v) }
        }
        RibbonField {
            compact: true
            objectName: "side" + sec.tag + "Size"
            fieldWidth: 66
            step: 2
            minValue: sec.sizeRange ? sec.sizeRange.min : null
            maxValue: sec.sizeRange ? sec.sizeRange.max : null
            value: Number(sec.own("size")) > 0 ? Number(sec.own("size")) : Number(sec.sel.font_size)
            onCommit: function (n) { sec.side.setProp(sec.line + "_size", n) }
        }
    }
    SidebarRow {
        wide: true
        label: ""
        RibbonStrip {
            id: styleStrip
            compact: true
            objectName: "side" + sec.tag + "Style"
            items: [
                { id: "bold", label: "B", bold: true, on: sec.flag("bold") },
                { id: "italic", label: "I", italic: true, on: sec.flag("italic") },
                { id: "underline", label: "U", underline: true, on: sec.flag("underline") }
            ]
            onClicked: function (id) { sec.side.setProp(sec.line + "_" + id, !sec.flag(id)) }
        }
        Item { width: sec.width - 20 - 8 - styleStrip.width - colorSwatch.width; height: 1 }
        RibbonSwatch {
            id: colorSwatch
            compact: true
            objectName: "side" + sec.tag + "Color"
            letter: "A"
            value: String(sec.own("color") || sec.sel.font_color || "")
            dialogTitle: sec.line === "active_line" ? qsTr("Active line color") : qsTr("Other lines color")
            onEdited: function (v) { sec.side.setProp(sec.line + "_color", v) }
        }
    }
}
