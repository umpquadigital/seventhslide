pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// The Cues tab: line control, the cues, the selected cue's label, and what
// the selected text box shows of them.
Row {
    id: tab
    required property SlideEditState dialog
    readonly property SlideCuesSession cues: tab.dialog.edit ? tab.dialog.edit.cues : null
    readonly property bool lineControl: !!(tab.cues && tab.cues.lineControl)
    readonly property var cue: tab.cues ? tab.cues.current : ({})
    readonly property bool hasCue: !!(tab.cue && tab.cue.text !== undefined)
    readonly property bool textBox: tab.dialog.hasSel && tab.dialog.selType === "text"
    spacing: 0
    height: parent ? parent.height : implicitHeight

    RibbonGroup {
        first: true
        height: parent.height
        row1: [
            RibbonButton {
                objectName: "ribbonLineControl"
                icon: "cue"
                label: qsTr("Line control")
                on: tab.lineControl
                onClicked: tab.dialog.toggleLineControl()
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonShowsCues"
                icon: "cue"
                label: qsTr("Shows cues")
                enabled: tab.lineControl && tab.textBox
                on: !!tab.dialog.sel.cue_linked
                onClicked: {
                    tab.dialog.endTextEdit(true)
                    tab.dialog.report(tab.cues.setLinked(!tab.dialog.sel.cue_linked))
                }
            }
        ]
    }
    RibbonGroup {
        height: parent.height
        enabled: tab.lineControl
        row1: [
            RibbonButton {
                objectName: "ribbonInsertCue"
                icon: "plus"
                label: qsTr("Insert cue")
                onClicked: {
                    tab.dialog.endTextEdit(true)
                    if (tab.dialog.report(tab.cues.insert("")))
                        tab.dialog.beginCueInlineRename(tab.cues.index)
                }
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonRenameCue"
                icon: "rename"
                label: qsTr("Rename")
                enabled: tab.hasCue
                onClicked: tab.dialog.beginCueInlineRename(tab.cues.index)
            },
            RibbonButton {
                objectName: "ribbonDeleteCue"
                icon: "trash"
                hint: qsTr("Delete cue")
                enabled: tab.hasCue && tab.cues.canDelete
                onClicked: tab.dialog.report(tab.cues.remove())
            }
        ]
    }
    RibbonGroup {
        height: parent.height
        enabled: tab.lineControl && tab.hasCue
        row1: [
            RibbonDropdown {
                objectName: "ribbonCueVerse"
                fieldWidth: 110
                options: {
                    const out = [{ value: "", label: qsTr("No label") }]
                    const types = tab.cues ? tab.cues.verseTypes : []
                    for (let i = 0; i < types.length; ++i)
                        out.push({ value: types[i], label: types[i] })
                    return out
                }
                value: String(tab.cue.verse_type || "")
                onPicked: function (v) { tab.dialog.report(tab.cues.setVerse(v, Number(tab.cue.verse_number))) }
            },
            NumberField {
                objectName: "ribbonCueVerseNumber"
                fieldWidth: 56
                minValue: 1
                maxValue: 99
                enabled: String(tab.cue.verse_type || "").length > 0
                value: Number(tab.cue.verse_number)
                onCommit: function (n) { tab.dialog.report(tab.cues.setVerse(String(tab.cue.verse_type), n)) }
            }
        ]
        row2: [
            NumberField {
                objectName: "ribbonCueChapter"
                label: qsTr("Bible")
                fieldWidth: 44
                stepper: false
                minValue: 0
                maxValue: 150
                value: Number(tab.cue.chapter)
                onCommit: function (n) { tab.dialog.report(tab.cues.setScripture(n, Number(tab.cue.verse))) }
            },
            NumberField {
                objectName: "ribbonCueVerseRef"
                fieldWidth: 44
                stepper: false
                minValue: 0
                maxValue: 176
                value: Number(tab.cue.verse)
                onCommit: function (n) { tab.dialog.report(tab.cues.setScripture(Number(tab.cue.chapter), n)) }
            },
            RibbonButton {
                objectName: "ribbonCueShowRef"
                label: qsTr("Show number")
                enabled: Number(tab.cue.chapter) > 0
                on: !!tab.cue.show_ref
                onClicked: tab.dialog.report(tab.cues.setRefShown(!tab.cue.show_ref))
            }
        ]
    }
    RibbonGroup {
        height: parent.height
        enabled: tab.lineControl && tab.textBox && !!tab.dialog.sel.cue_linked
        row1: [
            RibbonButton {
                objectName: "ribbonUpcoming"
                label: qsTr("Upcoming lines")
                on: !!tab.dialog.sel.show_upcoming
                onClicked: tab.dialog.report(tab.cues.setShowUpcoming(!tab.dialog.sel.show_upcoming))
            }
        ]
    }
    RibbonGroup {
        grow: true
        height: parent.height
        enabled: tab.lineControl && tab.textBox && !!tab.dialog.sel.cue_linked
        row1: [
            RibbonButton {
                objectName: "ribbonShowChords"
                icon: "note"
                label: qsTr("Chords over words")
                on: !!tab.dialog.sel.show_chords
                onClicked: tab.dialog.setProp("show_chords", !tab.dialog.sel.show_chords)
            }
        ]
        row2: [
            Row {
                spacing: 6
                visible: tab.dialog.editingChords
                height: Style.ribbonH
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Chord at caret")
                    color: Style.textCaption
                    font.pixelSize: Style.ribbonCaption
                }
                DialogField {
                    objectName: "ribbonCaretChord"
                    width: 70
                    text: tab.dialog.caretChord
                    placeholder: qsTr("none")
                    onCommitted: function (v) { tab.dialog.setCaretChord(v) }
                    onSubmitted: function (v) { tab.dialog.setCaretChord(v) }
                }
            }
        ]
    }
}
