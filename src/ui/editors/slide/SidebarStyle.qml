import QtQuick
import SeventhSlide

// Bold, italic and underline in the sidebar as one strip: the selection's
// flags ``prefix`` + each of ``parts``, each shown and turned over by its
// part ("bold").
RibbonStrip {
    id: strip
    required property SlideEditState dialog
    /// The flags' names before the part: "upcoming_" is upcoming_bold, ...
    property string prefix: ""
    property var parts: ["bold", "italic", "underline"]

    readonly property var glyphs: ({ bold: "B", italic: "I", underline: "U" })

    compact: true
    items: strip.parts.map(part => ({
        id: part,
        label: strip.glyphs[part],
        bold: part === "bold",
        italic: part === "italic",
        underline: part === "underline",
        on: !!strip.dialog.sel[strip.prefix + part]
    }))
    onClicked: function (part) {
        strip.dialog.setProp(strip.prefix + part, !strip.dialog.sel[strip.prefix + part])
    }
}
