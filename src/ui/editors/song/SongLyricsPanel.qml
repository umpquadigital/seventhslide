pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide
import "../../js/MultiSelect.js" as MultiSelect
import "../../js/Reorder.js" as Reorder

// Lyrics page of the song editor: Guided / XML on the title line; the title
// and play order as one form group; then one well split into the verses and
// the selected verse's editor (or, in XML, the lyrics as markup). The song
// lives in SongEditState (`dialog`); the verse editor's text handling and the
// order drags' pointer tracking are here, next to the widgets they drive.
Item {
    id: panel

    /// The song editor's state. Every `panel.dialog.` below is this
    /// panel's interface to it.
    required property SongEditState dialog
    /// ``dialog.session``, typed, so calls on it are checked.
    readonly property SessionFacade session: panel.dialog.session
    /// Where the order-drag ghost is drawn; ``orderGhostX``/``Y`` are in
    /// its coordinates.
    required property Item ghostLayer
    /// What takes focus when a field lets go of it.
    required property Item focusSink

    // Pointer position in ghostLayer coordinates; the floating ghost binds to it.
    property real orderGhostX: 0
    property real orderGhostY: 0

    // What the verse editor shows about its text: chord badges (painted over
    // the text at these rectangles) and whether the selection is formatted.
    property var verseChordMarks: []
    property bool verseFmtBold: false
    property bool verseFmtItalic: false
    property bool verseFmtUnderline: false

    // The chord labels' font (SongVerseEditor draws them), to space them out.
    FontMetrics {
        id: chordLabelMetrics
        font.pixelSize: Style.fontCaption
        font.bold: true
    }

    Connections {
        target: panel.dialog
        function onVerseEditorReloadRequested() {
            Qt.callLater(panel.hydrateVerseEditor)
        }
        function onVerseShown(index) {
            Qt.callLater(panel.scrollVerseIntoView, index)
        }
        function onOrderChipAdded() {
            Qt.callLater(function () {
                titleOrder.stripFlick.contentX = Math.max(0, titleOrder.stripFlick.contentWidth - titleOrder.stripFlick.width)
            })
        }
    }

    function clearFieldFocus() {
        titleOrder.titleBox.clearFocus()
        // Move focus off any DialogField TextInput onto the dialog shell.
        panel.focusSink.forceActiveFocus()
    }

    function refreshVerseEditorChrome() {
        const raw = VerseEditor.chordMarks(verseEditorPane.textEdit) || []
        const painted = []
        // Labels are wider than many words: on each line a label starts no
        // sooner than a small gap after the one before it. Marks come in
        // document order, so a line's labels come left to right.
        let lineY = NaN
        let labelsFreeAt = 0
        for (let i = 0; i < raw.length; ++i) {
            const m = raw[i]
            const pos = Number(m.position) || 0
            const len = Math.max(1, Number(m.length))
            const a = verseEditorPane.textEdit.positionToRectangle(pos)
            // Where the run ends: the start of the character after it.
            const b = verseEditorPane.textEdit.positionToRectangle(pos + len)
            const sameLine = Math.abs(a.y - b.y) < 1.5
            const w = sameLine ? (b.x - a.x) : Math.max(a.width, 8)
            const h = Math.max(12, Math.min(a.height, 16))
            const label = String(m.label || m.chord || "")
            if (!(Math.abs(a.y - lineY) < 1.5)) {
                lineY = a.y
                labelsFreeAt = a.x
            }
            const labelX = Math.max(a.x, labelsFreeAt)
            labelsFreeAt = labelX + chordLabelMetrics.advanceWidth(label) + 4
            painted.push({
                label: label,
                color: m.color,
                bare: Boolean(m.bare),
                x: a.x,
                labelX: labelX,
                y: a.y,
                width: Math.max(6, w),
                height: h
            })
        }
        panel.verseChordMarks = painted
        panel.verseFmtBold = VerseEditor.hasFormat(verseEditorPane.textEdit, "b")
        panel.verseFmtItalic = VerseEditor.hasFormat(verseEditorPane.textEdit, "i")
        panel.verseFmtUnderline = VerseEditor.hasFormat(verseEditorPane.textEdit, "u")
        if (!verseTools.chordInput.inputFocused)
            verseTools.chordInput.text = VerseEditor.chordAt(verseEditorPane.textEdit)
    }

    function hydrateVerseEditor() {
        const epoch = ++panel.dialog.verseEditorEpoch
        panel.dialog.verseEditorLoading = true
        if (panel.dialog.selectedVerse < 0 || panel.dialog.selectedVerse >= panel.dialog.verses.length) {
            VerseEditor.load(verseEditorPane.textEdit, "", panel.dialog.verseBreakLabel())
            panel.verseChordMarks = []
            panel.verseFmtBold = false
            panel.verseFmtItalic = false
            panel.verseFmtUnderline = false
            Qt.callLater(function () {
                if (epoch !== panel.dialog.verseEditorEpoch)
                    return
                panel.dialog.verseEditorLoading = false
            })
            return
        }
        const expected = String(panel.dialog.draftContent || "")
        VerseEditor.load(verseEditorPane.textEdit, expected, panel.dialog.verseBreakLabel())
        Qt.callLater(function () {
            if (epoch !== panel.dialog.verseEditorEpoch)
                return
            if (expected.length) {
                const got = String(VerseEditor.markup(verseEditorPane.textEdit) || "")
                if (!got.length)
                    VerseEditor.load(verseEditorPane.textEdit, expected, panel.dialog.verseBreakLabel())
            }
            Qt.callLater(function () {
                if (epoch !== panel.dialog.verseEditorEpoch)
                    return
                panel.dialog.verseEditorLoading = false
                panel.refreshVerseEditorChrome()
            })
        })
    }

    function captureVerseEditor() {
        if (panel.dialog.verseEditorLoading)
            return
        panel.dialog.verseEditorLoading = true
        VerseEditor.sanitizeBreaks(verseEditorPane.textEdit, panel.dialog.verseBreakLabel())
        VerseEditor.applySpacing(verseEditorPane.textEdit)
        panel.dialog.draftContent = VerseEditor.markup(verseEditorPane.textEdit)
        panel.dialog.verseEditorLoading = false
        panel.refreshVerseEditorChrome()
    }

    function handleVerseBreakKey(event) {
        const p = verseEditorPane.textEdit.cursorPosition
        const inBreak = VerseEditor.inBreak(verseEditorPane.textEdit)
        const prevBreak = p > 0 && VerseEditor.breakAt(verseEditorPane.textEdit, p - 1)
        if (event.key === Qt.Key_Backspace) {
            if (inBreak || prevBreak) {
                if (prevBreak && !inBreak)
                    verseEditorPane.textEdit.cursorPosition = p - 1
                VerseEditor.deleteBreak(verseEditorPane.textEdit)
                panel.captureVerseEditor()
                return true
            }
            return false
        }
        if (event.key === Qt.Key_Delete && inBreak) {
            VerseEditor.deleteBreak(verseEditorPane.textEdit)
            panel.captureVerseEditor()
            return true
        }
        if (inBreak && event.text && event.text.length && event.text !== "\u0008") {
            VerseEditor.placeCursorAfterBreak(verseEditorPane.textEdit)
            return false
        }
        return false
    }

    function applyVerseChord(name) {
        if (panel.dialog.selectedVerse < 0 || panel.dialog.selectedVerse >= panel.dialog.verses.length)
            return
        VerseEditor.setChord(verseEditorPane.textEdit, name)
        panel.captureVerseEditor()
        verseEditorPane.textEdit.forceActiveFocus()
    }

    /// Put the chord field back to the chord the cursor's word has, and give
    /// the words the keys again.
    function revertVerseChord() {
        verseTools.chordInput.text = VerseEditor.chordAt(verseEditorPane.textEdit)
        verseEditorPane.textEdit.forceActiveFocus()
    }

    function applyLyricFormat(tag) {
        if (panel.dialog.selectedVerse < 0 || panel.dialog.selectedVerse >= panel.dialog.verses.length)
            return
        VerseEditor.toggleFormat(verseEditorPane.textEdit, tag)
        panel.captureVerseEditor()
    }

    function insertForcedBreak() {
        if (panel.dialog.selectedVerse < 0 || panel.dialog.selectedVerse >= panel.dialog.verses.length)
            return
        VerseEditor.insertBreak(verseEditorPane.textEdit, panel.dialog.verseBreakLabel())
        panel.captureVerseEditor()
    }

    function trackOrderGhost(item, localX, localY) {
        if (!item)
            return
        const p = item.mapToItem(panel.ghostLayer, localX, localY)
        panel.orderGhostX = p.x
        panel.orderGhostY = p.y
    }

    function updateVerseOrderDrag(item, localX, localY) {
        if (!panel.dialog.verseDragActive || !item)
            return
        panel.trackOrderGhost(item, localX, localY)
        const op = item.mapToItem(titleOrder.strip, localX, localY)
        const overOrder = op.x >= 0 && op.y >= 0
                && op.x <= titleOrder.strip.width && op.y <= titleOrder.strip.height
        panel.dialog.orderDropHover = overOrder
        let insert = -1
        if (!overOrder) {
            const lp = item.mapToItem(verseListPane.list, localX, localY)
            const overList = lp.x >= 0 && lp.y >= 0
                    && lp.x <= verseListPane.list.width && lp.y <= verseListPane.list.height
            if (overList) {
                const cy = item.mapToItem(verseListPane.list.contentItem, localX, localY).y
                insert = Reorder.slotInView(verseListPane.list, cy)
                MultiSelect.autoScrollView(verseListPane.list, lp.y, 28, 14)
            }
        }
        panel.dialog.verseDropInsert = insert
    }

    function chipHitAt(item, localX, localY) {
        const n = panel.dialog.orderChips.length
        if (!item || n <= 0)
            return { hover: -1, before: false }
        const pt = item.mapToItem(titleOrder.chips, localX, localY)
        let rowHover = -1
        const kids = titleOrder.chips.children
        for (let i = 0; i < kids.length; ++i) {
            const child = kids[i]
            if (!child || child.modelData === undefined || child.index === undefined)
                continue
            const idx = child.index
            if (idx === panel.dialog.chipDragFrom)
                continue
            if (pt.y < child.y - 3 || pt.y > child.y + child.height + 3)
                continue
            if (pt.x < child.x + child.width / 2)
                return { hover: idx, before: true }
            rowHover = idx
        }
        if (rowHover >= 0)
            return { hover: rowHover, before: false }
        return { hover: -1, before: false }
    }

    function updateChipOrderDrag(item, localX, localY) {
        if (!panel.dialog.chipDragActive || !item)
            return
        panel.trackOrderGhost(item, localX, localY)
        const hit = panel.chipHitAt(item, localX, localY)
        panel.dialog.chipDropHover = hit.hover
        panel.dialog.chipDropBefore = hit.before
    }

    // Bring a verse row into view. Deferred so the list has laid out the new
    // delegate (rows size to their preview text) before we position on it.
    function scrollVerseIntoView(index) {
        if (index < 0 || index >= panel.dialog.verses.length)
            return
        verseListPane.list.forceLayout()
        verseListPane.list.positionViewAtIndex(index, ListView.Contain)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Lyrics")
            text: qsTr("Type each verse on its own, then add the verses to the order they play in.")
            tools: SegmentedControl {
                options: [qsTr("Guided"), qsTr("XML")]
                current: panel.dialog.lyricsMode === "raw" ? 1 : 0
                onChosen: function (index) {
                    panel.clearFieldFocus()
                    panel.dialog.setLyricsMode(index === 1 ? "raw" : "guided")
                }
            }
        }

        SongTitleOrder {
            id: titleOrder
            Layout.fillWidth: true
            dialog: panel.dialog
            panel: panel
        }

        // Guided, over the well as Settings' Stacks has its tools, each over
        // the pane it works on: over the verses, the selected verse's picker,
        // then adding a verse, duplicating it, deleting it and adding it to
        // the order, at the line between the panes; over the editor, the
        // tools for its words.
        RowLayout {
            Layout.fillWidth: true
            visible: panel.dialog.lyricsMode === "guided"
            spacing: 6

            // As wide as the verses, the well's edge and the line after them.
            RowLayout {
                Layout.preferredWidth: verseListPane.width + 2
                Layout.fillWidth: false
                spacing: 6

                // At most 120 wide, the rest the gap before the buttons, and
                // narrower when the column is, so they stay over the verses.
                SongVersePicker {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 90
                    Layout.maximumWidth: 120
                    dialog: panel.dialog
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    size: "control"
                    iconSource: Icons.url("plus", ink)
                    onClicked: panel.dialog.addVerse()
                }
                AppButton {
                    size: "control"
                    iconSource: Icons.url("copy", ink)
                    enabled: panel.dialog.hasVerseSelection
                    onClicked: panel.dialog.duplicateSelectedVerse()
                }
                AppButton {
                    size: "control"
                    iconSource: Icons.url("trash", ink)
                    enabled: panel.dialog.hasVerseSelection
                    onClicked: panel.dialog.removeSelectedVerse()
                }
                AppButton {
                    size: "control"
                    primary: true
                    label: qsTr("Order")
                    iconSource: Icons.url("listPlus", ink)
                    enabled: panel.dialog.hasVerseSelection
                    onClicked: panel.dialog.addToVerseOrder(panel.dialog.selectedVerse)
                }
            }
            SongVerseTools {
                id: verseTools
                Layout.fillWidth: true
                dialog: panel.dialog
                panel: panel
            }
        }

        // Guided: the verses beside the selected verse's editor.
        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: -4
            visible: panel.dialog.lyricsMode === "guided"

            SongVerseList {
                id: verseListPane
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(220, Math.floor(panel.width * 0.4))
                dialog: panel.dialog
                panel: panel
            }
            SongVerseEditor {
                id: verseEditorPane
                Layout.fillWidth: true
                Layout.fillHeight: true
                dialog: panel.dialog
                panel: panel
            }
        }

        // XML: the lyrics as markup.
        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.dialog.lyricsMode === "raw"
            border.color: rawLyricsEdit.activeFocus ? Style.focusEdge : Style.wellEdge

            WellPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                showHeader: false

                Flickable {
                    id: rawFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 8
                    readonly property int vGutter: Style.scrollGutter(rawFlick)
                    readonly property int hGutter: Style.scrollGutterH(rawFlick)
                    contentWidth: Math.max(width - vGutter, rawLyricsEdit.contentWidth + vGutter)
                    contentHeight: rawLyricsEdit.paintedHeight + 8 + hGutter
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar {}
                    ScrollBar.horizontal: AppScrollBar {}

                    TextEdit {
                        id: rawLyricsEdit
                        width: Math.max(rawFlick.width - rawFlick.vGutter, implicitWidth)
                        color: Style.text
                        font.family: Platform.monoFamily
                        font.pixelSize: Style.fontBody
                        wrapMode: TextEdit.NoWrap
                        selectByMouse: true
                        text: panel.dialog.rawLyrics
                        onTextChanged: panel.dialog.rawLyrics = text
                    }
                }
            }
        }
    }
}
