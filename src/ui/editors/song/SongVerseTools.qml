pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The selected verse's tools for its words, over the lyrics well's editor
// side, at its right, 6 apart: B / I / U as one strip (the slide editor's),
// the chord of the word the cursor is in, then a slide break.
// Its picker, adding, duplicating and deleting it are over the verses
// (SongVersePicker and SongLyricsPanel).
RowLayout {
    id: verseTools
    /// The song being edited.
    required property SongEditState dialog
    /// The lyrics panel, for the verse editor's helpers. Untyped, since
    /// SongLyricsPanel makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var panel // SongLyricsPanel
    /// The chord field, which follows the verse editor's cursor.
    readonly property alias chordInput: chordField

    spacing: 6

    Item { Layout.fillWidth: true }
    RibbonStrip {
        compact: true
        cellH: Style.controlH
        enabled: verseTools.dialog.hasVerseSelection
        opacity: enabled ? 1 : 0.4
        items: [
            { id: "b", label: "B", bold: true, on: verseTools.panel.verseFmtBold },
            { id: "i", label: "I", bold: true, italic: true, on: verseTools.panel.verseFmtItalic },
            { id: "u", label: "U", bold: true, underline: true, on: verseTools.panel.verseFmtUnderline }
        ]
        onClicked: function (id) { verseTools.panel.applyLyricFormat(id) }
    }
    // The chord of the word the cursor is in: type one and press Enter to
    // set it, or clear it to take it off; Escape puts it back.
    DialogField {
        id: chordField
        Layout.preferredWidth: 72
        enabled: verseTools.dialog.hasVerseSelection
        opacity: enabled ? 1 : 0.4
        placeholder: qsTr("Chord")
        escapeClearsFocus: true
        onInputFocusedChanged: {
            if (chordField.inputFocused)
                chordField.selectAll()
        }
        onEdited: function (v) { chordField.text = v }
        onSubmitted: function (v) { verseTools.panel.applyVerseChord(v) }
        onEscaped: verseTools.panel.revertVerseChord()
    }
    AppButton {
        size: "control"
        iconSource: Icons.url("break", ink)
        enabled: verseTools.dialog.hasVerseSelection
        onClicked: verseTools.panel.insertForcedBreak()
    }
}
