pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// The song editor's selected verse, as the right pane of the lyrics well: its
// words (its tools are over the well, SongVersePicker and SongVerseTools).
WellPane {
    id: verseEditor
    /// The song being edited.
    required property SongEditState dialog
    /// The lyrics panel, for the verse editor's and the drags' helpers.
    /// Untyped, since SongLyricsPanel makes this type and Qt refuses the
    /// cycle; tools/panel_refs.py checks the names used on it.
    required property var panel // SongLyricsPanel
    /// The verse text, which the panel's editing helpers work on.
    readonly property alias textEdit: selectedEdit

    ruleLeft: true
    title: qsTr("Words")

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: "transparent"

        // Inset as the verse list is, so the two panes' scrollbars match.
        Flickable {
            id: selectedFlick
            anchors.fill: parent
            anchors.margins: 6
            visible: verseEditor.dialog.selectedVerse >= 0
                     && verseEditor.dialog.selectedVerse < verseEditor.dialog.verses.length
            contentWidth: width - Style.scrollGutter(selectedFlick)
            // The words' own height: the editor below is at least the pane's
            // height, to click anywhere in, but only the words scroll. (Its
            // full height read as content that just fits, and the scrollbar
            // kept its gutter for good.)
            contentHeight: selectedEdit.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            ScrollBar.vertical: AppScrollBar {}

            TextEdit {
                id: selectedEdit
                width: selectedFlick.width - Style.scrollGutter(selectedFlick)
                // Fill the panel: TextEdit otherwise shrinks to painted text.
                height: Math.max(implicitHeight, selectedFlick.height)
                // 12 / 10 from the pane's edge, with the 6 inset above.
                leftPadding: 6
                rightPadding: 6
                topPadding: verseEditor.dialog.draftFirstLineHasChord ? 14 : 4
                bottomPadding: 4
                color: Style.text
                font.pixelSize: Style.fontBody
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                textFormat: TextEdit.RichText
                onTopPaddingChanged: Qt.callLater(verseEditor.panel.refreshVerseEditorChrome)
                // Keep the cursor in view as it moves past either edge.
                onCursorRectangleChanged: {
                    const r = selectedEdit.cursorRectangle
                    const most = Math.max(0, selectedFlick.contentHeight - selectedFlick.height)
                    if (r.y < selectedFlick.contentY)
                        selectedFlick.contentY = Math.max(0, r.y - selectedEdit.topPadding)
                    else if (r.y + r.height > selectedFlick.contentY + selectedFlick.height)
                        selectedFlick.contentY = Math.min(most, r.y + r.height - selectedFlick.height
                                                                + selectedEdit.bottomPadding)
                }
                persistentSelection: true
                z: 1
                Keys.priority: Keys.BeforeItem
                Component.onCompleted: verseEditor.panel.hydrateVerseEditor()
                onWidthChanged: {
                    if (verseEditor.dialog.verseEditorLoading || verseEditor.dialog.selectedVerse < 0)
                        return
                    VerseEditor.applySpacing(selectedEdit)
                    Qt.callLater(verseEditor.panel.refreshVerseEditorChrome)
                }
                onTextChanged: {
                    if (!verseEditor.dialog.verseEditorLoading && verseEditor.dialog.selectedVerse >= 0)
                        verseEditor.panel.captureVerseEditor()
                }
                onCursorPositionChanged: {
                    if (verseEditor.dialog.verseEditorLoading || verseEditor.dialog.verseBreakSnapping)
                        return
                    if (VerseEditor.inBreak(selectedEdit)) {
                        verseEditor.dialog.verseBreakSnapping = true
                        VerseEditor.selectBreak(selectedEdit)
                        verseEditor.dialog.verseBreakSnapping = false
                    }
                    verseEditor.panel.refreshVerseEditorChrome()
                }
                Keys.onPressed: function (event) {
                    if (verseEditor.panel.handleVerseBreakKey(event)) {
                        event.accepted = true
                        return
                    }
                    if (!(event.modifiers & Qt.ControlModifier))
                        return
                    if (event.key === Qt.Key_B) {
                        verseEditor.panel.applyLyricFormat("b")
                        event.accepted = true
                    } else if (event.key === Qt.Key_I) {
                        verseEditor.panel.applyLyricFormat("i")
                        event.accepted = true
                    } else if (event.key === Qt.Key_U) {
                        verseEditor.panel.applyLyricFormat("u")
                        event.accepted = true
                    }
                }
            }

            Item {
                id: chordPaint
                x: selectedEdit.x
                y: selectedEdit.y
                width: selectedEdit.width
                height: selectedEdit.height
                z: 2

                Repeater {
                    model: verseEditor.panel.verseChordMarks
                    delegate: Rectangle {
                        required property var modelData
                        // A bare chord has no word under it to wash.
                        visible: !modelData.bare
                        x: Number(modelData.x) || 0
                        y: Number(modelData.y) || 0
                        width: Math.max(6, Number(modelData.width) || 0)
                        height: Math.max(12, Number(modelData.height) || 0)
                        radius: Style.radiusSmall
                        color: String(modelData.color)
                        opacity: 0.20
                    }
                }
                Repeater {
                    model: verseEditor.panel.verseChordMarks
                    delegate: Text {
                        required property var modelData
                        x: Number(modelData.labelX) || 0
                        y: Math.max(0, (Number(modelData.y) || 0) - implicitHeight - 2)
                        text: String(modelData.label || "")
                        color: Qt.lighter(String(modelData.color), 1.35)
                        font.pixelSize: Style.fontCaption
                        font.bold: true
                    }
                }
            }
        }
    }
}
