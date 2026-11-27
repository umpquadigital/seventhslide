pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect

// The library's Bibles tab: a translation picker in the toolbar, a reference
// typed into the search field (resolved live, with book-name suggestions) and
// the chapter's verses, selected with the usual list rules and dragged (or
// added) onto the service.
LibraryTab {
    id: tab

    /// The search field, for the suggestions to drop down from.
    property Item searchAnchor: null

    /// Save verses ``verseStart``-``verseEnd`` as a new Freeform item.
    signal saveAsFreeformRequested(int verseStart, int verseEnd, string name)
    signal menusCloseRequested()

    searchPlaceholder: qsTr("Search a reference…")
    onSearchChanged: {
        if (tab.current)
            tab.liveSearch()
    }
    toolbarExtra: translationPicker

    addableSelectionCount: tab.selectedVerses.length
    offersConvert: true
    canConvert: tab.addItemCount === 1 && !!tab.session && tab.session.library.selectedBibleId >= 0
    exportChoices: [{ id: "images", label: qsTr("As images…"), icon: "image" }]
    canExport: tab.addItemCount > 0 && !!tab.session && tab.session.library.selectedBibleId >= 0

    // The reference the search field resolved to.
    property int verseStart: 1
    property int verseEnd: 1
    property bool refResolved: false

    /// The selected verses of the current chapter (by verse number, in
    /// reading order; may be non-contiguous).
    readonly property ListSelection verseSelection: ListSelection {
        model: tab.session ? tab.session.library.bibleVerses : null
        keyRole: BibleVerseListModel.VerseNumRole
        rowOrder: true
        view: verseList
    }
    readonly property var selectedVerses: tab.verseSelection.keys
    /// How many service items Add makes: one per run of consecutive verses.
    readonly property int addItemCount: MultiSelect.consecutiveRuns(tab.selectedVerses).length

    /// Book names the typed reference could mean.
    property var suggestions: []
    onSuggestionsChanged: {
        suggestFlick.contentY = 0
        tab.suggestKeysY = 0
        if (tab.current && tab.suggestions.length > 0) {
            if (!suggestPopup.visible)
                suggestPopup.open()
        } else if (suggestPopup.visible) {
            suggestPopup.close()
        }
    }
    property int suggestIndex: 0
    /// Where in the window the pointer last was over the list.
    property point suggestPointer
    /// Where the arrow keys last left the list scrolled to.
    property real suggestKeysY: 0
    /// The translation button's text: the selected Bible's full name (its
    /// abbreviation only when it has no name); the menu's rows show both.
    readonly property string translationLabel: {
        const lib = tab.session ? tab.session.library : null
        if (!lib || lib.selectedBibleId < 0)
            return qsTr("Select bible…")
        return lib.selectedBibleName || lib.selectedBibleAbbreviation || qsTr("Bible")
    }

    // Row rhythm.
    readonly property int rowGap: 3
    readonly property int rowPadV: 10

    function enter() {
        tab.suggestions = []
        tab.liveSearch()
    }

    function leave() {
        tab.suggestions = []
    }

    function clearSelection() {
        tab.verseSelection.clear()
        tab.verseSelection.endMarquee()
    }

    function closeMenus() {
        tab.menusCloseRequested()
    }

    function convertSelected() {
        if (!tab.canConvert)
            return
        const runs = tab.verseRuns()
        if (!runs.length)
            return
        const label = tab.dragLabel()
        const name = label.length ? (label + qsTr(" (edit)")) : qsTr("Untitled")
        tab.saveAsFreeformRequested(runs[0].start, runs[0].end, name)
    }

    /// The selected verses as images: a passage per run of verses, as Add
    /// makes service items.
    function exportChosen(id) {
        if (id !== "images" || !tab.canExport)
            return
        const ranges = tab.verseRuns().map(r => ({ verseStart: r.start, verseEnd: r.end }))
        const label = tab.dragLabel()
        tab.exportImages(qsTr("%1, a PNG image per slide, in a ZIP file.").arg(label), label,
                         (outputId, file) => tab.session.exporter.exportBibleImages(ranges, outputId,
                                                                                    file))
    }

    function addToService() {
        if (!tab.canAdd)
            return
        const payload = tab.dragPayload()
        if (!payload)
            return
        if (!tab.applyLibraryPayload(payload) && tab.session) {
            const ranges = (payload.ranges && payload.ranges.length)
                           ? payload.ranges
                           : [{ verseStart: payload.verseStart, verseEnd: payload.verseEnd }]
            for (let i = 0; i < ranges.length; ++i)
                tab.session.library.addBibleToService(ranges[i].verseStart, ranges[i].verseEnd)
        }
    }

    function searchKeyPressed(key) {
        if (!tab.suggestions.length)
            return false
        if (key === Qt.Key_Down) {
            tab.stepSuggestion(1)
            return true
        }
        if (key === Qt.Key_Up) {
            tab.stepSuggestion(-1)
            return true
        }
        if (key === Qt.Key_Tab) {
            tab.acceptSuggestion(tab.suggestions[tab.clampedSuggestIndex()])
            return true
        }
        return false
    }

    /// Return in the search field: take the highlighted suggestion, or play
    /// the reference.
    function searchSubmitted() {
        if (tab.suggestions.length > 0) {
            tab.acceptSuggestion(tab.suggestions[tab.clampedSuggestIndex()])
            return true
        }
        if (!tab.refResolved)
            tab.liveSearch()
        if (tab.session && tab.refResolved)
            tab.session.live.playBible(tab.verseStart, tab.verseEnd)
        return true
    }

    function searchEscaped() {
        if (!tab.suggestions.length)
            return false
        tab.suggestions = []
        return true
    }

    /// Move the highlight by ``step`` rows, scrolling the list to keep it in
    /// sight.
    function stepSuggestion(step) {
        tab.suggestIndex = Math.max(0, Math.min(tab.suggestions.length - 1,
                                                tab.suggestIndex + step))
        const r = suggestRows.itemAt(tab.suggestIndex)
        if (!r)
            return
        if (r.y < suggestFlick.contentY)
            suggestFlick.contentY = r.y
        else if (r.y + r.height > suggestFlick.contentY + suggestFlick.height)
            suggestFlick.contentY = r.y + r.height - suggestFlick.height
        tab.suggestKeysY = suggestFlick.contentY
    }

    /// The pointer over row ``index`` takes the highlight, unless it has not
    /// moved in the window and the list is where the arrow keys left it: the
    /// row slid under a still pointer, which is not the operator pointing at
    /// it. The wheel moving the list is, and the highlight stays in sight.
    function pointAtSuggestion(area, index) {
        const p = area.mapToItem(null, area.mouseX, area.mouseY)
        if (p.x === tab.suggestPointer.x && p.y === tab.suggestPointer.y
                && suggestFlick.contentY === tab.suggestKeysY)
            return
        tab.suggestPointer = p
        tab.suggestIndex = index
    }

    function clampedSuggestIndex() {
        return Math.max(0, Math.min(tab.suggestIndex, tab.suggestions.length - 1))
    }

    function pickBible(id) {
        if (!tab.session)
            return
        tab.session.library.selectBible(id)
        tab.closeMenus()
        tab.verseStart = 1
        tab.verseEnd = 1
        tab.verseSelection.clear()
        tab.refResolved = false
        tab.liveSearch()
    }

    /// The book part of a typed reference (``1 Cor 13:4`` -> ``1 Cor``).
    function bookToken(raw) {
        let s = String(raw || "").trim()
        s = s.replace(/[:.\-\s]+$/g, "")
        const m = s.match(/^(.*?)(?:\s+\d[\d\s:.\-]*)$/)
        return (m ? m[1] : s).trim()
    }

    /// Resolve the typed reference as it changes; suggest books while it
    /// does not resolve.
    function liveSearch() {
        if (!tab.session)
            return
        tab.closeMenus()
        tab.refResolved = false
        if (tab.session.library.selectedBibleId < 0) {
            tab.suggestions = []
            tab.verseSelection.clear()
            return
        }
        const res = tab.session.library.resolveBibleRef(tab.search)
        if (res && res.success) {
            tab.refResolved = true
            tab.verseStart = res.verse_start
            tab.verseEnd = res.verse_end
            tab.verseSelection.clear()
            tab.suggestions = []
            return
        }
        tab.refreshSuggestions()
    }

    function refreshSuggestions() {
        if (!tab.session || tab.session.library.selectedBibleId < 0 || tab.refResolved) {
            tab.suggestions = []
            return
        }
        const token = tab.bookToken(tab.search)
        if (!token.length) {
            tab.suggestions = []
            return
        }
        const hits = tab.session.library.suggestBibleBooks(token) || []
        if (hits.length === 1 && String(hits[0]).toLowerCase() === token.toLowerCase()) {
            tab.suggestions = []
            return
        }
        tab.suggestions = hits
        tab.suggestIndex = 0
    }

    /// Put the book ``name`` in place of the typed one, keeping the rest.
    function acceptSuggestion(name) {
        if (!name || !name.length)
            return
        const raw = tab.search
        const token = tab.bookToken(raw)
        let rest = ""
        if (token.length) {
            const idx = raw.toLowerCase().indexOf(token.toLowerCase())
            if (idx >= 0)
                rest = raw.slice(idx + token.length).replace(/^[:.\-\s]+/, "").trim()
        }
        tab.search = rest.length ? (name + " " + rest) : (name + " ")
        tab.suggestions = []
    }

    function verseRuns() {
        return MultiSelect.consecutiveRuns(tab.selectedVerses)
    }

    function dragPayload() {
        const runs = tab.verseRuns()
        const ranges = []
        for (let i = 0; i < runs.length; ++i)
            ranges.push({ verseStart: runs[i].start, verseEnd: runs[i].end })
        if (!ranges.length)
            return null
        if (ranges.length === 1)
            return { kind: "bible", verseStart: ranges[0].verseStart, verseEnd: ranges[0].verseEnd }
        return { kind: "bible", ranges: ranges }
    }

    function dragLabel() {
        const book = tab.session && tab.session.library.selectedBibleBook
                     ? String(tab.session.library.selectedBibleBook) : ""
        const ch = tab.session ? tab.session.library.selectedBibleChapter : 0
        const runs = tab.verseRuns()
        if (!runs.length)
            return qsTr("Bible")
        const prefix = book.length ? (book + " " + ch + ":") : ""
        const parts = []
        for (let i = 0; i < runs.length; ++i) {
            if (runs[i].end > runs[i].start)
                parts.push(runs[i].start + "-" + runs[i].end)
            else
                parts.push(String(runs[i].start))
        }
        return prefix + parts.join(", ")
    }

    ListView {
        id: verseList
        anchors.fill: parent
        clip: true
        spacing: tab.rowGap
        boundsBehavior: Flickable.StopAtBounds
        interactive: !tab.verseSelection.marqueeActive && !tab.libDragging
        ScrollBar.vertical: AppScrollBar {}
        model: tab.session ? tab.session.library.bibleVerses : null
        delegate: Rectangle {
            id: verseRow
            required property int verseNum
            required property string text

            readonly property int gap: 14
            readonly property bool selected: tab.verseSelection.contains(verseRow.verseNum)

            width: verseList.width - Style.scrollGutter(verseList)
            height: Math.ceil(verseText.contentHeight) + tab.rowPadV
            color: Style.panelRowBg(verseRow.selected, verseHover.containsMouse)
            radius: Style.radiusSmall
            ListRowAccent {
                cornerRadius: parent.radius
                selected: verseRow.selected
                hovered: verseHover.containsMouse
            }

            Text {
                id: verseNumLabel
                x: verseRow.gap
                width: Math.ceil(contentWidth)
                anchors.verticalCenter: parent.verticalCenter
                text: String(verseRow.verseNum)
                color: Style.text
                font.pixelSize: Style.fontBody
                font.bold: true
            }

            Text {
                id: verseText
                x: verseNumLabel.x + verseNumLabel.width + verseRow.gap
                width: Math.max(1, verseRow.width - x - verseRow.gap)
                anchors.verticalCenter: parent.verticalCenter
                text: verseRow.text
                color: Style.text
                font.pixelSize: Style.fontBody
                wrapMode: Text.WordWrap
                lineHeight: 1.35
                lineHeightMode: Text.ProportionalHeight
            }

            LibraryRowPress {
                id: verseHover
                libraryTab: tab
                selection: tab.verseSelection
                key: verseRow.verseNum
                canDrag: tab.serviceOpen
                onDragStarted: function (x, y) {
                    tab.beginLibAdd(tab.dragPayload(), tab.dragLabel(), verseHover, x, y)
                }
                onActivated: function (live) {
                    if (tab.session)
                        tab.session.live.playBible(verseRow.verseNum, verseRow.verseNum, live)
                }
            }
        }

        MarqueeRect {
            view: verseList
            active: tab.verseSelection.marqueeActive
            contentX1: tab.verseSelection.marqueeX1
            contentY1: tab.verseSelection.marqueeY1
            contentX2: tab.verseSelection.marqueeX2
            contentY2: tab.verseSelection.marqueeY2
        }

        Text {
            anchors.centerIn: parent
            visible: verseList.count === 0
                     && (!tab.session || tab.session.library.enabledBibles.count === 0)
            z: 1
            text: tab.session && tab.session.library.bibles.count > 0
                  ? qsTr("Every Bible is switched off in Settings")
                  : qsTr("No Bibles imported")
            color: Style.textMuted
            font.pixelSize: Style.fontUi
        }
    }

    // Empty space draws a marquee.
    LibraryMarqueeArea {
        libraryTab: tab
        list: verseList
        selection: tab.verseSelection
    }

    // The translation picker, at the start of the toolbar.
    Component {
        id: translationPicker

        Item {
            implicitHeight: Style.controlH

            DropdownField {
                id: selectBtn
                anchors.fill: parent
                text: tab.translationLabel
                open: menu.visible
                onPressed: tab.interacted()
                onClicked: {
                    tab.suggestions = []
                    if (menu.visible)
                        menu.close()
                    else
                        menu.openAt()
                }
            }

            Connections {
                target: tab
                function onMenusCloseRequested() {
                    if (menu.visible)
                        menu.close()
                }
            }

            AppMenu {
                id: menu
                objectName: "translationMenu"
                anchorItem: selectBtn
                minWidth: Math.max(120, selectBtn.width)
                maxHeight: 280

                Repeater {
                    model: menu.visible && tab.session ? tab.session.library.enabledBibles : null
                    delegate: MenuItemRow {
                        id: bibleOpt
                        required property var bibleId
                        required property string name
                        required property string abbreviation
                        checkable: true
                        checked: tab.session !== null
                                 && tab.session.library.selectedBibleId === bibleOpt.bibleId
                        label: bibleOpt.name
                        shortcut: bibleOpt.abbreviation
                        onTriggered: tab.pickBible(bibleOpt.bibleId)
                    }
                }
            }
        }
    }

    Popup {
        id: suggestPopup
        objectName: "bookSuggestions"
        parent: Overlay.overlay
        padding: 4
        modal: false
        dim: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.max(120, tab.searchAnchor ? tab.searchAnchor.width : 0)
        height: Math.min(180, suggestCol.implicitHeight + topPadding + bottomPadding)
        onAboutToShow: {
            const o = Overlay.overlay
            const a = tab.searchAnchor
            if (!o || !a)
                return
            const p = a.mapToItem(o, 0, a.height + 2)
            x = p.x
            y = p.y
            width = Math.max(120, a.width)
        }
        onClosed: {
            if (tab.suggestions.length)
                tab.suggestions = []
        }
        background: Rectangle {
            radius: Style.radiusSmall
            color: Style.menuBg
            border.color: Style.menuEdge
        }
        contentItem: Flickable {
            id: suggestFlick
            clip: true
            contentWidth: width - Style.scrollGutter(suggestFlick)
            contentHeight: suggestCol.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {}

            Column {
                id: suggestCol
                width: suggestFlick.width - Style.scrollGutter(suggestFlick)
                spacing: 1

                Repeater {
                    id: suggestRows
                    model: tab.suggestions
                    delegate: Rectangle {
                        id: suggestRow
                        required property int index
                        required property var modelData
                        width: suggestCol.width
                        height: 22
                        radius: Style.radiusSmall
                        color: tab.suggestIndex === suggestRow.index
                               ? Style.menuCurrent
                               : (suggestHover.containsMouse ? Style.menuHover : "transparent")
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            verticalAlignment: Text.AlignVCenter
                            text: String(suggestRow.modelData)
                            color: suggestHover.containsMouse || tab.suggestIndex === suggestRow.index
                                   ? Style.textStrong : Style.text
                            font.pixelSize: Style.fontSmall
                            elide: Text.ElideRight
                        }
                        MouseArea {
                            id: suggestHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onEntered: tab.pointAtSuggestion(suggestHover, suggestRow.index)
                            onPositionChanged: tab.pointAtSuggestion(suggestHover, suggestRow.index)
                            onClicked: tab.acceptSuggestion(String(suggestRow.modelData))
                        }
                    }
                }
            }
        }
    }
}
