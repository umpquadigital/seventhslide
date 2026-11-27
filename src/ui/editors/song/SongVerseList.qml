pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// The song editor's verses, in lyric order, as the left pane of the lyrics
// well: pick one to edit, drag one into the play order or to another place.
// Add and Order are over the well, in SongLyricsPanel.
// Each row shows the verse's code and name, then every line of its words.
WellPane {
    id: listPane
    /// The song being edited.
    required property SongEditState dialog
    /// The lyrics panel, for the verse editor's and the drags' helpers.
    /// Untyped, since SongLyricsPanel makes this type and Qt refuses the
    /// cycle; tools/panel_refs.py checks the names used on it.
    required property var panel // SongLyricsPanel
    /// The verse list, which the panel's drag helper maps onto.
    readonly property alias list: verseList

    title: qsTr("Verses")

    // Inset in the well as the Library's lists are, its scrollbar in the
    // inset beside the rows.
    ListView {
        id: verseList
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.margins: 6
        rightMargin: Style.scrollGutter(verseList)
        spacing: 3
        clip: true
        model: listPane.dialog.verses
        boundsBehavior: Flickable.StopAtBounds
        interactive: !listPane.dialog.verseDragActive && !listPane.dialog.chipDragActive
        ScrollBar.vertical: AppScrollBar {}

        delegate: PickRow {
            id: verseRow
            required property int index
            required property var modelData

            readonly property bool selectedRow: listPane.dialog.selectedVerse === index
            // Where a dragged verse would land: before this row, or after the
            // last. Not next to where it started, which would not move it.
            readonly property bool landsHere: listPane.dialog.verseDragActive
                                              && !listPane.dialog.orderDropHover
                                              && listPane.dialog.verseDropInsert !== listPane.dialog.verseDragFrom
                                              && listPane.dialog.verseDropInsert !== listPane.dialog.verseDragFrom + 1
            readonly property string words: {
                const raw = verseRow.selectedRow ? listPane.dialog.draftContent
                                                 : String(modelData.content || "")
                return listPane.dialog.versePlainPreview(raw)
            }

            code: listPane.dialog.verseCodeAt(verseRow.index)
            title: SongText.verseDisplayName(verseRow.modelData.label)
            subtitle: verseRow.words.length ? verseRow.words : qsTr("(empty)")
            subtitleLines: 0
            subtitleInk: verseRow.words.length ? (selected ? Style.textSoft : Style.textCaption)
                                               : Style.textFaint
            selected: verseRow.selectedRow
            draggable: true
            lifted: listPane.dialog.verseDragActive && listPane.dialog.verseDragFrom === index
            dropAbove: verseRow.landsHere && listPane.dialog.verseDropInsert === index
            dropBelow: verseRow.landsHere && index === verseList.count - 1
                       && listPane.dialog.verseDropInsert === verseList.count

            onClicked: {
                listPane.panel.clearFieldFocus()
                listPane.dialog.selectVerse(verseRow.index)
            }
            onDragStarted: function (x, y) {
                listPane.dialog.beginVerseOrderDrag(verseRow.index)
                listPane.panel.clearFieldFocus()
                listPane.panel.updateVerseOrderDrag(verseRow, x, y)
            }
            onDragMoved: function (x, y) { listPane.panel.updateVerseOrderDrag(verseRow, x, y) }
            onDropped: function (x, y) {
                listPane.panel.updateVerseOrderDrag(verseRow, x, y)
                listPane.dialog.finishVerseOrderDrag()
            }
            onDragCanceled: listPane.dialog.cancelVerseOrderDrag()
        }

        Text {
            visible: verseList.count === 0
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            // 14 from the well, with the list's inset.
            anchors.margins: 8
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("No verses yet. Use + to add one.")
            color: Style.textCaption
            font.pixelSize: Style.fontBody
        }
    }
}
