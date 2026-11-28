import QtQuick
import SeventhSlide
import "../../js/Reorder.js" as Reorder

// The song editor's view-model: the song being edited (title, lyrics in
// the guided and raw views, play order and arrangements, authors, the
// per-chip theme maps) and every edit to it. SongEditDialog is the sheet
// around it and its panels show it.
//
// What only the view can do is asked for: the verse editor reloads on
// ``verseEditorReloadRequested`` and scrolls on ``verseShown``; the
// dialog opens its "Other credit" prompt on ``customRoleAsked``, and the
// Details page puts the cursor in a new author's name on ``authorAdded``.
QtObject {
    id: state

    property SessionFacade session
    /// The theme map editor the Themes tab edits a chip's map in: the
    /// selected theme chip's per-output overrides, { outputId: { text?, bg? } }.
    property ThemeMapEditor themeMapEditor
    readonly property AuthorListModel authorModel: AuthorListModel {}

    /// The verse editor should reload from ``draftContent``.
    signal verseEditorReloadRequested()
    /// Verse ``index`` should be scrolled into view.
    signal verseShown(int index)
    /// A chip was added at the end of the play order.
    signal orderChipAdded()
    /// The operator wants to type a free-text author credit; pass it to
    /// addSelectedAuthorCustomRole.
    signal customRoleAsked()
    /// A name is needed for an arrangement, ``mode`` "new" or "rename",
    /// starting from ``initial``; pass it to submitArrangementName.
    signal arrangementNameAsked(string mode, string initial)
    /// Author ``index`` was just added, open and unnamed.
    signal authorAdded(int index)


    // Authors & Copyright (library songs only)
    property string songbookName: ""
    property string songbookEntry: ""
    property string copyrightText: ""
    property string ccliSongNumber: ""
    property string keyText: ""
    property bool showCopyright: false
    // Topics & Comments (library songs only)
    /// The song's topics, as typed (core tidies them when it saves).
    property var topics: []
    property string commentsText: ""

    property string rawLyrics: ""
    property string rawOrder: ""
    property int selectedOrderIndex: -1
    property string titleText: ""
    property bool verseBreakSnapping: false

    property bool open: false
    property bool libraryMode: false
    property var itemId: -1
    property var songId: -1
    property string errorText: ""
    /// What Save would have written when the song was opened (``savedForm``).
    property string loadedForm: ""
    property string tab: "lyrics" // lyrics | authors | topics | themes
    property string lyricsMode: "guided" // guided | raw
    property var verses: []          // [{label, content}, ...]
    property var orderChips: []      // ["V1", "C1", ...]
    property int selectedVerse: -1
    onSelectedVerseChanged: state.verseEditorReloadRequested()
    property string draftContent: ""
    // Extra TextEdit topPadding is only for a chord label on the first lyric
    // line. Later lines already get block topMargin from applyVerseEditorSpacing.
    readonly property bool draftFirstLineHasChord: SongText.firstLineHasChord(state.draftContent)
    property bool verseEditorLoading: false
    property int verseEditorEpoch: 0
    readonly property bool hasVerseSelection: state.selectedVerse >= 0
                                              && state.selectedVerse < state.verses.length
    readonly property int selectedTypeIndex: state.hasVerseSelection
        ? SongText.verseTypeIndex(state.verses[state.selectedVerse].label) : 0
    readonly property int selectedVerseNumber: state.hasVerseSelection
        ? SongText.verseNumber(state.verses[state.selectedVerse].label) : 1
    // Drag verse -> reorder list, or drop on the play-order strip
    property int verseDragFrom: -1
    property bool verseDragActive: false
    property bool orderDropHover: false
    property int verseDropInsert: -1
    property string verseDragCode: ""
    // Drag chips within the play-order strip to reorder
    property int chipDragFrom: -1
    property bool chipDragActive: false
    property int chipDropHover: -1
    property bool chipDropBefore: false
    property string chipDragCode: ""
    property bool chipDragTitle: false
    property bool chipDragTheme: false
    readonly property bool orderGhostActive: state.verseDragActive || state.chipDragActive
    /// The author picked in the Details table, or -1.
    property int selectedAuthorIndex: -1
    property var chipMaps: ({})          // { TH1: { outputId: { text?, bg? } } }
    property var arrangements: []        // [{id, name, order, chips}]
    property string activeArrangementId: ""
    property string selectedThemeCode: ""
    /// Where in the play order the selected theme change was picked: a chip
    /// can play more than once, and what its Previous takes depends on where.
    property int selectedThemeIndex: -1
    /// ``selectedThemeIndex`` while it still holds the selected chip, else the
    /// chip's first place in the play order; -1 when it has none.
    readonly property int selectedThemePlace: state.themeChipPlace(state.selectedThemeCode,
                                                                   state.selectedThemeIndex)
    readonly property var themeChipList: {
        const chips = state.orderChips
        const out = []
        for (let i = 0; i < chips.length; ++i) {
            const c = String(chips[i] || "")
            if (SongText.isThemeCode(c))
                out.push({ code: c.toUpperCase(), orderIndex: i })
        }
        return out
    }
    readonly property string activeArrangementName: {
        for (let i = 0; i < state.arrangements.length; ++i) {
            if (String(state.arrangements[i].id) === state.activeArrangementId)
                return state.arrangementLabel(state.arrangements[i].name)
        }
        return state.arrangementLabel("")
    }
    /// What a theme change's slot takes when it has no theme of its own:
    /// Previous (unset), what the change before it played with, or Above
    /// (SongText.inheritAboveId), the next tier in the theme priority. The
    /// first change has none before it, so its Previous is Above.
    readonly property var themeInheritChoices: [
        { id: "", slotName: qsTr("Previous"), caption: qsTr("Inherit from Previous"),
          label: qsTr("As the theme change before"), previous: true },
        { id: SongText.inheritAboveId, slotName: qsTr("Above"), caption: qsTr("Inherit from Above"),
          label: qsTr("Next in the theme priority"), previous: false }
    ]
    /// What the selected theme change's Previous takes, by output: what the
    /// change before it in the play order played with.
    readonly property var previousThemeMap: SongText.previousThemeMap(
        state.chipMapsAsEdited(), state.orderChips.map(String), state.selectedThemePlace)
    readonly property bool hasAuthorSelection: state.selectedAuthorIndex >= 0
                                               && state.selectedAuthorIndex < state.authorModel.count
    /// Display wording for the roles the menu offers, in menu order. Mirrors
    /// ``model::k_author_role_*``; core still validates what is saved, so this
    /// list only decides what can be picked, never what is legal.
    readonly property var authorRoleOptions: [
        { key: "words", label: qsTr("Words") },
        { key: "music", label: qsTr("Music") },
        { key: "translation", label: qsTr("Translation") },
        { key: "arrangement", label: qsTr("Arrangement") }
    ]

    function verseBreakLabel() {
        return qsTr("Slide break")
    }

    function versePlainPreview(raw) {
        return VerseEditor.plainPreview(String(raw || ""))
    }

    /// Open on the song of rundown item ``id``; it saves to that service only.
    function openItem(id) {
        if (!state.session || id < 0)
            return
        const data = state.session.library.getServiceSongItem(id)
        if (!data || data.itemId === undefined)
            return
        state.libraryMode = false
        state.itemId = data.itemId
        state.songId = -1
        state.loadFields(data.title || "", data.lyrics || "", data.verseOrder || "")
        state.loadThemeMap(data)
        state.clearAuthorFields()
        state.errorText = ""
        state.tab = "lyrics"
        state.loadedForm = state.savedForm()
        state.open = true
    }

    /// Open on library song ``id``.
    function openLibrary(id) {
        if (!state.session || id < 0)
            return
        const data = state.session.library.getSong(id)
        if (!data || data.songId === undefined)
            return
        state.libraryMode = true
        state.itemId = -1
        state.songId = data.songId
        state.loadFields(data.title || "", data.lyrics || "", data.verseOrder || "")
        state.loadAuthorFields(data)
        state.loadThemeMap(data)
        state.errorText = ""
        state.tab = "lyrics"
        state.loadedForm = state.savedForm()
        state.open = true
    }

    /// Open on a new, empty library song.
    function openNewLibrary() {
        if (!state.session)
            return
        state.libraryMode = true
        state.itemId = -1
        state.songId = -1
        state.loadFields("", "", "")
        state.clearAuthorFields()
        state.loadThemeMap(null)
        state.errorText = ""
        state.tab = "lyrics"
        state.loadedForm = state.savedForm()
        state.open = true
    }

    function loadFields(title, lyrics, order) {
        state.verseEditorLoading = true
        state.verseEditorEpoch += 1
        state.titleText = title
        state.rawLyrics = lyrics
        state.rawOrder = order
        state.selectedVerse = -1
        state.draftContent = ""
        state.lyricsMode = "guided"
        state.syncRawToGuided()
    }

    function clearAuthorFields() {
        state.selectedAuthorIndex = -1
        state.authorModel.clear()
        state.songbookName = ""
        state.songbookEntry = ""
        state.copyrightText = ""
        state.ccliSongNumber = ""
        state.keyText = ""
        state.showCopyright = false
        state.topics = []
        state.commentsText = ""
    }

    function loadAuthorFields(data) {
        state.selectedAuthorIndex = -1
        state.authorModel.clear()
        state.authorModel.load((data && data.authors) ? data.authors : [])
        state.songbookName = (data && data.songbookName) ? String(data.songbookName) : ""
        state.songbookEntry = (data && data.songbookEntry) ? String(data.songbookEntry) : ""
        state.copyrightText = (data && data.copyright) ? String(data.copyright) : ""
        state.ccliSongNumber = (data && data.ccliSongNumber) ? String(data.ccliSongNumber) : ""
        state.keyText = (data && data.key) ? String(data.key) : ""
        state.showCopyright = !!(data && data.showCopyright)
        state.topics = (data && data.topics) ? data.topics.map(String) : []
        state.commentsText = (data && data.comments) ? String(data.comments) : ""
    }

    /// Add topic ``text``, unless the song has it already (in any case): as a
    /// save stores it (SongText.withTopic).
    function addTopic(text) {
        const next = SongText.withTopic(state.topics, String(text || ""))
        if (next.length !== state.topics.length)
            state.topics = next
    }

    function removeTopic(index) {
        if (index < 0 || index >= state.topics.length)
            return
        const next = state.topics.slice()
        next.splice(index, 1)
        state.topics = next
    }

    function loadThemeMap(data) {
        const pack = (data && data.themeMap) ? data.themeMap : {}
        if (pack.arrangements && pack.arrangements.length) {
            // Each has an id, an order and chips, and active is one of them
            // (theme::SongThemeMap); a name may be empty.
            const arrs = []
            for (let i = 0; i < pack.arrangements.length; ++i) {
                const a = pack.arrangements[i]
                arrs.push({
                    id: String(a.id),
                    name: String(a.name || SongText.defaultArrangementName),
                    order: String(a.order),
                    chips: state.cloneChipMaps(a.chips)
                })
            }
            state.arrangements = arrs
            const active = String(pack.active)
            state.activeArrangementId = active
            const cur = state.findArrangement(active)
            state.chipMaps = cur ? state.cloneChipMaps(cur.chips) : {}
            if (state.orderChips.length === 0 && cur && String(cur.order || "").trim().length) {
                state.rawOrder = String(cur.order)
                state.orderChips = SongText.orderTokens(String(cur.order))
            }
            state.selectedThemeCode = ""
            state.selectedThemeIndex = -1
            state.themeMapEditor.themeMap = {}
            state.ensureThemeChip(false)
            return
        }
        state.arrangements = [{
            id: "a1",
            name: SongText.defaultArrangementName,
            order: state.orderChips.join(" "),
            chips: {}
        }]
        state.activeArrangementId = "a1"
        state.chipMaps = {}
        state.selectedThemeCode = ""
        state.selectedThemeIndex = -1
        state.themeMapEditor.themeMap = {}
        state.ensureThemeChip(false)
    }

    function cloneChipMaps(src) {
        const next = {}
        const keys = Object.keys(src || {})
        for (let i = 0; i < keys.length; ++i) {
            const code = String(keys[i] || "").toUpperCase()
            if (!SongText.isThemeCode(code))
                continue
            next[code] = Object.assign({}, src[keys[i]] || {})
        }
        return next
    }

    function findArrangement(id) {
        const want = String(id || "")
        for (let i = 0; i < state.arrangements.length; ++i) {
            if (String(state.arrangements[i].id) === want)
                return state.arrangements[i]
        }
        return null
    }

    function persistActiveArrangement() {
        const id = state.activeArrangementId
        if (!id.length)
            return
        state.flushSelectedChipMap()
        if (state.arrangements.some(a => String(a.id) === id))
            state.arrangements = state.arrangementsAsEdited()
    }

    /// The arrangements with the active one as the editor holds it (its
    /// order and chips), leaving the editor as it is.
    function arrangementsAsEdited() {
        const id = state.activeArrangementId
        if (!id.length)
            return state.arrangements
        const order = state.lyricsMode === "raw"
                    ? String(state.rawOrder || "").trim()
                    : state.orderChips.join(" ")
        return state.arrangements.map(a => String(a.id) !== id ? a : {
            id: a.id,
            name: a.name,
            order: order,
            chips: state.cloneChipMaps(state.chipMapsAsEdited())
        })
    }

    /// The theme map Save stores for the arrangements (SongText.packThemeMap).
    function packedThemeMap() {
        state.persistActiveArrangement()
        return SongText.packThemeMap(state.arrangements, state.activeArrangementId)
    }

    /// An arrangement's name as the editor shows it: the unnamed default reads
    /// "Default" in the operator's language, though it is stored the same in all.
    function arrangementLabel(name) {
        const stored = String(name || "")
        return (!stored.length || stored === SongText.defaultArrangementName) ? qsTr("Default")
                                                                              : stored
    }

    function flushSelectedChipMap() {
        if (SongText.isThemeCode(String(state.selectedThemeCode || "").toUpperCase()))
            state.chipMaps = state.chipMapsAsEdited()
    }

    /// The chip maps with the selected chip's as its editor holds it, leaving
    /// the editor as it is.
    function chipMapsAsEdited() {
        const code = String(state.selectedThemeCode || "").toUpperCase()
        if (!SongText.isThemeCode(code))
            return state.chipMaps
        const maps = Object.assign({}, state.chipMaps)
        maps[code] = Object.assign({}, state.themeMapEditor.themeMap || {})
        return maps
    }

    /// Select theme chip ``code``, the one at ``orderIndex`` in the play order
    /// when given, and load its map into the editor.
    function syncThemeMapFromChip(code, orderIndex) {
        const id = String(code || "").toUpperCase()
        state.selectedThemeCode = SongText.isThemeCode(id) ? id : ""
        if (orderIndex !== undefined)
            state.selectedThemeIndex = orderIndex
        if (!state.selectedThemeCode.length) {
            state.themeMapEditor.themeMap = {}
            return
        }
        state.themeMapEditor.themeMap = Object.assign({}, (state.chipMaps && state.chipMaps[id]) || {})
    }

    /// ``orderIndex`` when the play order has theme chip ``code`` there, else
    /// the chip's first place in it; -1 when it has none.
    function themeChipPlace(code, orderIndex) {
        const want = String(code || "").toUpperCase()
        if (!want.length)
            return -1
        const at = function (i) { return String(state.orderChips[i] || "").toUpperCase() === want }
        if (orderIndex >= 0 && orderIndex < state.orderChips.length && at(orderIndex))
            return orderIndex
        for (let i = 0; i < state.orderChips.length; ++i) {
            if (at(i))
                return i
        }
        return -1
    }

    function themeChipCodes() {
        const out = []
        for (let i = 0; i < state.orderChips.length; ++i) {
            const c = String(state.orderChips[i] || "").toUpperCase()
            if (SongText.isThemeCode(c))
                out.push(c)
        }
        return out
    }

    function ensureThemeChip(selectInOrder) {
        const codes = state.themeChipCodes()
        if (!codes.length) {
            state.selectedThemeCode = ""
            state.selectedThemeIndex = -1
            state.themeMapEditor.themeMap = {}
            return
        }
        const cur = String(state.selectedThemeCode || "").toUpperCase()
        const code = codes.indexOf(cur) >= 0 ? cur : codes[0]
        state.syncThemeMapFromChip(code)
        if (selectInOrder === false)
            return
        for (let i = 0; i < state.orderChips.length; ++i) {
            if (String(state.orderChips[i] || "").toUpperCase() === code) {
                state.selectedOrderIndex = i
                return
            }
        }
    }

    /// Credits are a set - one person can be words *and* arrangement - so the
    /// menu toggles and stays open instead of picking one and closing.
    function toggleSelectedAuthorRole(type) {
        if (!state.hasAuthorSelection)
            return
        state.authorModel.toggleCredit(state.selectedAuthorIndex, type)
    }

    /// Add a free-text credit; typing one that is already there is a no-op
    /// rather than the toggle the menu row does.
    function addSelectedAuthorCustomRole(label) {
        if (!state.hasAuthorSelection)
            return
        state.authorModel.addCustomCredit(state.selectedAuthorIndex, String(label || ""))
    }

    /// Free-text credits (an imported "Paraphrase", or one typed here) are told
    /// apart by their wording, so that is what toggles them.
    function toggleSelectedAuthorCustomRole(label) {
        if (!state.hasAuthorSelection)
            return
        state.authorModel.toggleCustomCredit(state.selectedAuthorIndex, String(label || ""))
    }

    /// Pick author ``index`` (letting go of the one picked before).
    function selectAuthor(index) {
        if (index < 0 || index >= state.authorModel.count)
            return
        if (index !== state.selectedAuthorIndex)
            state.closeAuthor()
        state.selectedAuthorIndex = Math.min(index, state.authorModel.count - 1)
    }

    /// Let go of the author picked. One added and left without a name goes.
    function closeAuthor() {
        const index = state.selectedAuthorIndex
        state.selectedAuthorIndex = -1
        if (index >= 0 && index < state.authorModel.count
                && !state.authorModel.nameAt(index).trim().length)
            state.removeAuthor(index)
    }

    /// Name author ``index`` ``value``; an empty name leaves the old one.
    function renameAuthor(index, value) {
        const name = String(value || "").trim()
        if (name.length)
            state.setAuthorAt(index, name)
    }

    /// Author ``index``'s name edit ended with ``value``: it is the name when
    /// it has one, and an author still without a name (one just added) goes.
    function finishAuthorName(index, value) {
        if (index < 0 || index >= state.authorModel.count)
            return
        state.renameAuthor(index, value)
        if (!state.authorModel.nameAt(index).trim().length)
            state.removeAuthor(index)
    }

    function authorMeta() {
        return Object.assign(state.authorFields(), { themeMap: state.packedThemeMap() })
    }

    /// Details as Save keeps them, the theme map aside.
    function authorFields() {
        return {
            authors: state.authorModel.authors(),
            songbookName: state.songbookName,
            songbookEntry: state.songbookEntry,
            copyright: state.copyrightText,
            ccliSongNumber: state.ccliSongNumber,
            key: state.keyText,
            showCopyright: state.showCopyright,
            topics: state.topics,
            comments: state.commentsText
        }
    }

    function setAuthorAt(index, value) {
        if (index < 0 || index >= state.authorModel.count)
            return
        state.authorModel.setName(index, value)
    }

    /// A new, unnamed author, open for its name.
    function addAuthor() {
        state.closeAuthor()
        state.authorModel.append("")
        state.selectedAuthorIndex = state.authorModel.count - 1
        state.authorAdded(state.selectedAuthorIndex)
    }

    function removeAuthor(index) {
        if (index < 0 || index >= state.authorModel.count)
            return
        state.authorModel.remove(index)
        if (state.selectedAuthorIndex === index)
            state.selectedAuthorIndex = -1
        else if (state.selectedAuthorIndex > index)
            state.selectedAuthorIndex -= 1
    }

    /// Back to closed, with nothing half-done.
    function close() {
        state.selectedAuthorIndex = -1
        state.cancelVerseOrderDrag()
        state.cancelChipOrderDrag()
        state.open = false
        state.errorText = ""
        state.selectedVerse = -1
    }

    function syncGuidedToRaw() {
        state.commitDraft()
        state.rawLyrics = SongText.lyrics(state.verses)
        state.rawOrder = state.orderChips.join(" ")
    }

    function syncRawToGuided() {
        state.selectedVerse = -1
        state.draftContent = ""
        state.verses = SongText.verses(state.rawLyrics)
        const orderText = String(state.rawOrder || "").trim()
        state.orderChips = SongText.orderTokens(orderText)
        if (state.verses.length > 0)
            state.selectVerse(0)
        state.verseEditorReloadRequested()
    }

    function setLyricsMode(mode) {
        if (state.lyricsMode === mode)
            return
        if (state.lyricsMode === "guided" && mode === "raw")
            state.syncGuidedToRaw()
        else if (state.lyricsMode === "raw" && mode === "guided")
            state.syncRawToGuided()
        state.lyricsMode = mode
        if (mode === "guided")
            state.verseEditorReloadRequested()
    }

    function remapOrderCodes(fromCode, toCode, swap) {
        const a = String(fromCode || "").toUpperCase()
        const b = String(toCode || "").toUpperCase()
        if (!a.length || a === b)
            return
        const next = state.orderChips.slice()
        for (let i = 0; i < next.length; ++i) {
            const c = String(next[i] || "").toUpperCase()
            if (c === a)
                next[i] = b
            else if (swap && c === b)
                next[i] = a
        }
        state.orderChips = next
    }

    function applyVerseIdentity(index, typeName, number) {
        if (index < 0 || index >= state.verses.length)
            return
        const canonType = SongText.verseTypeName(typeName)
        let n = Math.max(1, Math.min(99, Number(number)))
        state.commitDraft()
        const oldLabel = state.verses[index].label
        const oldType = SongText.verseTypeName(oldLabel)
        const oldNumber = SongText.verseNumber(oldLabel)
        const oldCode = state.verseCodeAt(index)
        const other = SongText.findVerse(state.verses, canonType, n, index)
        const next = state.verses.slice()
        if (other >= 0 && String(oldType).toLowerCase() === String(canonType).toLowerCase()) {
            const newLabel = SongText.makeVerseLabel(canonType, n)
            const otherLabel = SongText.makeVerseLabel(oldType, oldNumber)
            next[index] = { label: newLabel, content: next[index].content }
            next[other] = { label: otherLabel, content: next[other].content }
            state.verses = next
            state.remapOrderCodes(oldCode, SongText.baseVerseCode(SongText.labelToCode(newLabel)).toUpperCase(), true)
            if (state.selectedVerse === index)
                state.draftContent = next[index].content || ""
            else if (state.selectedVerse === other)
                state.draftContent = next[other].content || ""
            return
        }
        if (other >= 0)
            n = SongText.nextFreeVerseNumber(state.verses, canonType, n, index)
        if (n < 0) {
            state.errorText = qsTr("Every %1 number up to 99 is taken.").arg(canonType)
            return
        }
        const newLabel = SongText.makeVerseLabel(canonType, n)
        if (String(newLabel).toLowerCase() === String(oldLabel || "").toLowerCase())
            return
        next[index] = { label: newLabel, content: next[index].content }
        state.verses = next
        state.remapOrderCodes(oldCode, SongText.baseVerseCode(SongText.labelToCode(newLabel)).toUpperCase(), false)
        if (state.selectedVerse === index)
            state.draftContent = next[index].content || ""
    }

    function setSelectedVerseType(typeIndex) {
        if (!state.hasVerseSelection)
            return
        const typeName = SongText.verseTypes[typeIndex]
        state.applyVerseIdentity(state.selectedVerse, typeName, state.selectedVerseNumber)
    }

    function setSelectedVerseNumber(number) {
        if (!state.hasVerseSelection)
            return
        state.applyVerseIdentity(state.selectedVerse, SongText.verseTypeName(
                                    state.verses[state.selectedVerse].label), number)
    }

    function chipLabel(code) {
        if (SongText.isThemeCode(code))
            return qsTr("Th")
        if (SongText.isTitleCode(code))
            return qsTr("Ti")
        return String(code || "")
    }

    function nextThemeChipCode() {
        return SongText.nextThemeCode(state.orderChips.map(String))
    }

    function nextArrangementId() {
        const taken = {}
        for (let i = 0; i < state.arrangements.length; ++i)
            taken[String(state.arrangements[i].id)] = true
        let n = 1
        while (taken["a" + n])
            n += 1
        return "a" + n
    }

    function insertOrderChip(code) {
        const next = state.orderChips.slice()
        next.push(code)
        state.orderChips = next
        state.selectedOrderIndex = next.length - 1
        state.orderChipAdded()
    }

    function selectOrderChip(index, opts) {
        if (index < 0 || index >= state.orderChips.length)
            return
        const code = String(state.orderChips[index] || "")
        state.selectedOrderIndex = index
        if (SongText.isThemeCode(code)) {
            state.flushSelectedChipMap()
            state.syncThemeMapFromChip(code, index)
            if (!(opts && opts.stayOnLyrics))
                state.tab = "themes"
            return
        }
        if (SongText.isTitleCode(code))
            return
        const vi = state.findVerseIndexByCode(code)
        if (vi >= 0)
            state.selectVerse(vi)
        state.selectedOrderIndex = index
        if (!(opts && opts.stayOnLyrics))
            state.tab = "lyrics"
    }

    function findVerseIndexByCode(code) {
        const want = String(code || "").toUpperCase()
        for (let i = 0; i < state.verses.length; ++i) {
            if (state.verseCodeAt(i) === want)
                return i
        }
        return -1
    }

    function chipsAfterTheme(index) {
        const out = []
        for (let i = index + 1; i < state.orderChips.length; ++i) {
            const c = String(state.orderChips[i] || "")
            if (SongText.isThemeCode(c))
                break
            out.push(c)
        }
        return out
    }

    function commitDraft() {
        if (state.verseEditorLoading)
            return
        if (state.selectedVerse < 0 || state.selectedVerse >= state.verses.length)
            return
        state.verses = state.versesWithDraft()
    }

    function selectVerse(index) {
        if (index < 0 || index >= state.verses.length) {
            state.commitDraft()
            state.selectedVerse = -1
            state.draftContent = ""
            return
        }
        const nextContent = (state.verses[index] && state.verses[index].content) || ""
        if (state.selectedVerse === index) {
            if (nextContent === state.draftContent)
                return
            state.draftContent = nextContent
            state.verseEditorReloadRequested()
            return
        }
        state.commitDraft()
        state.selectedVerse = index
        state.draftContent = nextContent
        const code = state.verseCodeAt(index)
        for (let i = 0; i < state.orderChips.length; ++i) {
            if (String(state.orderChips[i] || "").toUpperCase() === code) {
                state.selectedOrderIndex = i
                break
            }
        }
    }

    function addToVerseOrder(index) {
        if (index < 0 || index >= state.verses.length)
            return
        const code = SongText.baseVerseCode(SongText.labelToCode(state.verses[index].label)).toUpperCase()
        state.orderChips = state.orderChips.concat([code])
    }

    function verseCodeAt(index) {
        if (index < 0 || index >= state.verses.length)
            return ""
        return SongText.baseVerseCode(SongText.labelToCode(state.verses[index].label)).toUpperCase()
    }

    function beginVerseOrderDrag(index) {
        if (index < 0 || index >= state.verses.length)
            return
        state.cancelChipOrderDrag()
        state.verseDragFrom = index
        state.verseDragCode = state.verseCodeAt(index)
        state.verseDragActive = true
        state.orderDropHover = false
        state.verseDropInsert = -1
    }

    function finishVerseOrderDrag() {
        if (!state.verseDragActive)
            return
        const from = state.verseDragFrom
        const dropOrder = state.orderDropHover
        const insertAt = state.verseDropInsert
        state.cancelVerseOrderDrag()
        if (dropOrder && from >= 0) {
            state.addToVerseOrder(from)
            return
        }
        if (insertAt >= 0 && from >= 0)
            state.moveVerse(from, insertAt)
    }

    function cancelVerseOrderDrag() {
        state.verseDragActive = false
        state.verseDragFrom = -1
        state.verseDragCode = ""
        state.orderDropHover = false
        state.verseDropInsert = -1
    }

    function moveOrderChip(from, insertBefore) {
        const to = Reorder.landing(from, insertBefore, state.orderChips.length)
        if (to >= 0)
            state.orderChips = Reorder.moved(state.orderChips, from, to)
    }

    function beginChipOrderDrag(index) {
        if (index < 0 || index >= state.orderChips.length)
            return
        state.cancelVerseOrderDrag()
        const code = String(state.orderChips[index] || "")
        state.chipDragFrom = index
        state.chipDragCode = code
        state.chipDragTitle = SongText.isTitleCode(code)
        state.chipDragTheme = SongText.isThemeCode(code)
        state.chipDragActive = true
        state.chipDropHover = -1
        state.chipDropBefore = false
    }

    function finishChipOrderDrag() {
        if (!state.chipDragActive)
            return
        const from = state.chipDragFrom
        let insertAt = state.chipDropHover
        const before = state.chipDropBefore
        state.cancelChipOrderDrag()
        if (insertAt < 0)
            return
        if (!before)
            insertAt += 1
        state.moveOrderChip(from, insertAt)
    }

    function cancelChipOrderDrag() {
        state.chipDragActive = false
        state.chipDragFrom = -1
        state.chipDragCode = ""
        state.chipDragTitle = false
        state.chipDragTheme = false
        state.chipDropHover = -1
        state.chipDropBefore = false
    }

    function addTitleChip() {
        state.insertOrderChip("T1")
    }

    function addThemeChip() {
        const code = state.nextThemeChipCode()
        const maps = Object.assign({}, state.chipMaps)
        maps[code] = {}
        state.chipMaps = maps
        state.insertOrderChip(code)
        state.flushSelectedChipMap()
        state.syncThemeMapFromChip(code, state.orderChips.length - 1)
    }

    function removeOrderChip(index) {
        if (index < 0 || index >= state.orderChips.length)
            return
        const code = String(state.orderChips[index] || "").toUpperCase()
        const next = state.orderChips.slice()
        next.splice(index, 1)
        state.orderChips = next
        if (SongText.isThemeCode(code)) {
            const maps = Object.assign({}, state.chipMaps)
            delete maps[code]
            state.chipMaps = maps
            if (String(state.selectedThemeCode || "").toUpperCase() === code)
                state.ensureThemeChip(false)
        }
        if (state.selectedOrderIndex === index)
            state.selectedOrderIndex = Math.min(index, next.length - 1)
        else if (state.selectedOrderIndex > index)
            state.selectedOrderIndex -= 1
        if (state.selectedThemeIndex > index)
            state.selectedThemeIndex -= 1
    }

    function selectArrangement(id) {
        const want = String(id || "")
        if (!want.length || want === state.activeArrangementId)
            return
        if (!state.findArrangement(want))
            return
        state.persistActiveArrangement()
        state.activeArrangementId = want
        const a = state.findArrangement(want)
        const orderText = String((a && a.order) || "").trim()
        state.orderChips = SongText.orderTokens(orderText)
        state.rawOrder = state.orderChips.join(" ")
        state.chipMaps = state.cloneChipMaps((a && a.chips) || {})
        state.selectedOrderIndex = state.orderChips.length ? 0 : -1
        state.selectedThemeCode = ""
        state.selectedThemeIndex = -1
        state.themeMapEditor.themeMap = {}
        state.ensureThemeChip(false)
    }

    function beginNewArrangement() {
        state.arrangementNameAsked("new", qsTr("Arrangement %1").arg(state.arrangements.length + 1))
    }

    function beginRenameArrangement() {
        state.arrangementNameAsked("rename", state.activeArrangementName)
    }

    /// Name a new arrangement (``mode`` "new") or the one shown ("rename").
    function submitArrangementName(mode: string, typed: string) {
        const name = typed.trim()
        if (!name.length)
            return
        if (mode === "new") {
            state.persistActiveArrangement()
            const id = state.nextArrangementId()
            const next = state.arrangements.slice()
            next.push({
                id: id,
                name: name,
                order: state.orderChips.join(" "),
                chips: state.cloneChipMaps(state.chipMaps)
            })
            state.arrangements = next
            state.activeArrangementId = id
            return
        }
        const arrs = state.arrangements.slice()
        for (let i = 0; i < arrs.length; ++i) {
            if (String(arrs[i].id) !== state.activeArrangementId)
                continue
            arrs[i] = {
                id: arrs[i].id,
                // The draft starts as the shown name: unchanged, keep what is stored.
                name: name === state.arrangementLabel(arrs[i].name) ? arrs[i].name : name,
                order: arrs[i].order,
                chips: arrs[i].chips
            }
            state.arrangements = arrs
            return
        }
    }

    function duplicateArrangement() {
        state.persistActiveArrangement()
        const src = state.findArrangement(state.activeArrangementId)
        if (!src)
            return
        const id = state.nextArrangementId()
        const next = state.arrangements.slice()
        next.push({
            id: id,
            name: qsTr("%1 copy").arg(state.arrangementLabel(src.name)),
            order: state.orderChips.join(" "),
            chips: state.cloneChipMaps(state.chipMaps)
        })
        state.arrangements = next
        state.activeArrangementId = id
    }

    function deleteArrangement() {
        if (state.arrangements.length < 2)
            return
        const id = state.activeArrangementId
        const next = []
        for (let i = 0; i < state.arrangements.length; ++i) {
            if (String(state.arrangements[i].id) !== id)
                next.push(state.arrangements[i])
        }
        if (!next.length)
            return
        state.arrangements = next
        state.activeArrangementId = String(next[0].id)
        const a = next[0]
        const orderText = String(a.order || "").trim()
        state.orderChips = SongText.orderTokens(orderText)
        state.rawOrder = state.orderChips.join(" ")
        state.chipMaps = state.cloneChipMaps(a.chips || {})
        state.selectedOrderIndex = state.orderChips.length ? 0 : -1
        state.selectedThemeCode = ""
        state.selectedThemeIndex = -1
        state.themeMapEditor.themeMap = {}
        state.ensureThemeChip(false)
    }

    function removeSelectedThemeChip() {
        const code = String(state.selectedThemeCode || "").toUpperCase()
        if (!SongText.isThemeCode(code))
            return
        for (let i = 0; i < state.orderChips.length; ++i) {
            if (String(state.orderChips[i] || "").toUpperCase() === code) {
                state.removeOrderChip(i)
                return
            }
        }
    }

    function addVerse() {
        state.commitDraft()
        const n = SongText.nextFreeVerseNumber(state.verses, "Verse", 1, -1)
        if (n < 0) {
            state.errorText = qsTr("Every %1 number up to 99 is taken.").arg("Verse")
            return
        }
        const label = SongText.makeVerseLabel("Verse", n)
        state.verses = state.verses.concat([{ label: label, content: "" }])
        state.selectedVerse = state.verses.length - 1
        state.draftContent = ""
        state.verseShown(state.selectedVerse)
    }

    function removeSelectedVerse() {
        state.removeVerse(state.selectedVerse)
    }

    /// A copy of the selected verse after it, as the next free number of its
    /// type, and select the copy.
    function duplicateSelectedVerse() {
        if (!state.hasVerseSelection)
            return
        state.commitDraft()
        const from = state.selectedVerse
        const type = SongText.verseTypeName(state.verses[from].label)
        const n = SongText.nextFreeVerseNumber(state.verses, type, 1, -1)
        if (n < 0) {
            state.errorText = qsTr("Every %1 number up to 99 is taken.").arg(type)
            return
        }
        const copy = { label: SongText.makeVerseLabel(type, n), content: state.verses[from].content }
        const next = state.verses.slice()
        next.splice(from + 1, 0, copy)
        state.verses = next
        state.selectedVerse = from + 1
        state.draftContent = copy.content || ""
        state.verseShown(state.selectedVerse)
    }

    /// The numbers other verses of type ``typeName`` use, in order, for the
    /// verse picker to show beside its number field.
    function verseNumbersInUse(typeName) {
        const type = SongText.verseTypeName(typeName).toLowerCase()
        const out = []
        for (let i = 0; i < state.verses.length; ++i) {
            if (i === state.selectedVerse)
                continue
            if (SongText.verseTypeName(state.verses[i].label).toLowerCase() === type)
                out.push(SongText.verseNumber(state.verses[i].label))
        }
        return out.sort(function (a, b) { return a - b })
    }

    function removeVerse(index) {
        if (index < 0 || index >= state.verses.length)
            return
        state.commitDraft()
        const next = state.verses.slice()
        next.splice(index, 1)
        state.verses = next
        if (next.length === 0) {
            state.selectedVerse = -1
            state.draftContent = ""
        } else if (state.selectedVerse >= next.length) {
            state.selectedVerse = next.length - 1
            state.draftContent = next[state.selectedVerse].content || ""
        } else if (state.selectedVerse === index) {
            state.draftContent = next[state.selectedVerse].content || ""
        } else if (state.selectedVerse > index) {
            state.selectedVerse -= 1
        }
    }

    function moveVerse(from, insertBefore) {
        const to = Reorder.landing(from, insertBefore, state.verses.length)
        if (to < 0)
            return
        state.commitDraft()
        const item = state.verses[from]
        state.verses = Reorder.moved(state.verses, from, to)
        state.selectedVerse = to
        state.draftContent = item.content || ""
    }

    /// The verses with the one being typed into as it stands (what
    /// ``commitDraft`` keeps), leaving the editor as it is.
    function versesWithDraft() {
        if (state.verseEditorLoading || state.selectedVerse < 0
                || state.selectedVerse >= state.verses.length)
            return state.verses
        const next = state.verses.slice()
        next[state.selectedVerse] = { label: next[state.selectedVerse].label,
                                      content: state.draftContent }
        return next
    }

    /// What Save would write, as one string; it changes nothing.
    function savedForm() {
        const guided = state.lyricsMode === "guided"
        const lyrics = guided ? SongText.lyrics(state.versesWithDraft()) : state.rawLyrics
        const order = guided ? state.orderChips.join(" ") : state.rawOrder
        const themes = SongText.packThemeMap(state.arrangementsAsEdited(), state.activeArrangementId)
        const rest = state.libraryMode ? Object.assign(state.authorFields(), { themeMap: themes })
                                       : themes
        return JSON.stringify([state.titleText, lyrics, order, rest])
    }

    /// Whether closing now would lose anything.
    function hasChanges() {
        return state.open && state.savedForm() !== state.loadedForm
    }

    /// Write the song back; false (with ``errorText`` set) when it was
    /// refused.
    function save() {
        if (!state.session)
            return false
        if (state.lyricsMode === "guided")
            state.syncGuidedToRaw()
        if (state.libraryMode) {
            const meta = state.authorMeta()
            if (state.songId >= 0) {
                const err = state.session.library.updateSong(
                            state.songId, state.titleText, state.rawLyrics, state.rawOrder, meta)
                if (err.length) {
                    state.errorText = err
                    return false
                }
            } else {
                const result = state.session.library.createSong(
                            state.titleText, state.rawLyrics, state.rawOrder, meta)
                if (result && result.error) {
                    state.errorText = result.error
                    return false
                }
            }
            return true
        }
        const err = state.session.library.updateServiceSongItem(
                    state.itemId, state.titleText, state.rawLyrics, state.rawOrder,
                    state.packedThemeMap())
        if (err.length) {
            state.errorText = err
            return false
        }
        return true
    }

    function askCustomRole() {
        state.customRoleAsked()
    }
}
