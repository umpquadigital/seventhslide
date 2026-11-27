pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// The slide editor's view-model: what the window shows beside the document
// (session.slideEdit), which is the ribbon tab, the sidebar, the zoom, a
// status line, and the text being typed into a box. Each menu keeps whether
// it is open itself. Nothing here is the document; every edit is one call
// into session.slideEdit.
//
// Values from the document come by the property tables' names (snake_case),
// as they paint. ``sel`` is the first selected object's, or a new object's
// with nothing selected.
QtObject {
    id: state

    property SessionFacade session
    readonly property SlideEditSession edit: state.session ? state.session.slideEdit : null
    readonly property bool active: !!(state.edit && state.edit.active)
    /// "" | freeform | text | bg.
    readonly property string mode: state.edit ? state.edit.mode : ""
    readonly property bool isFreeform: state.mode === "freeform"
    readonly property bool isTextTheme: state.mode === "text"
    readonly property bool isTheme: state.mode === "text" || state.mode === "bg"
    /// A background theme: a fill, no objects to put on it.
    readonly property bool isBgTheme: state.mode === "bg"
    readonly property int canvasW: state.edit && state.active ? state.edit.canvasWidth : 1920
    readonly property int canvasH: state.edit && state.active ? state.edit.canvasHeight : 1080

    // ---- the window ------------------------------------------------------
    /// home | insert | design | transitions | animations | cues.
    property string ribbonTab: "home"
    /// properties | build | notes.
    property string sidebarTab: "properties"

    // ---- builds -------------------------------------------------------------
    /// The current slide's animations, in its order (edit.animations.list).
    readonly property var animationList: state.edit && state.isFreeform ? state.edit.animations.list : []
    /// The animation picked in the Build pane: {objectId, order}, or null.
    property var selectedAnimation: null
    /// What the Animations tab edits: the one picked, else the selected
    /// object's first; null when the selection has none.
    readonly property var currentAnimation: {
        const list = state.animationList || []
        const id = state.sel ? String(state.sel.id || "") : ""
        if (!id.length)
            return null
        // The one picked, while its object is the one selected.
        const pick = state.selectedAnimation
        if (pick && pick.objectId === id) {
            for (let i = 0; i < list.length; ++i) {
                if (list[i].objectId === pick.objectId && list[i].order === pick.order)
                    return list[i]
            }
        }
        for (let j = 0; j < list.length; ++j) {
            if (list[j].objectId === id)
                return list[j]
        }
        return null
    }
    function selectAnimation(entry) {
        if (!entry || !state.edit)
            return
        state.selectedAnimation = { objectId: entry.objectId, order: entry.order }
        state.edit.objects.selectObject(entry.objectId, false)
    }
    /// The gallery's pick: the current animation becomes ``effect``, or, when
    /// the selection has none, ``effect`` is added to it.
    function pickEffect(effect) {
        if (!state.edit)
            return
        const now = state.currentAnimation
        if (now) {
            state.report(state.edit.animations.set(now.objectId, now.order, "effect", effect))
            return
        }
        state.addAnimation(effect)
    }
    /// ``effect`` added to the selection, the one added picked.
    function addAnimation(effect) {
        if (!state.edit)
            return
        const error = state.edit.animations.add(effect)
        state.report(error)
        const list = state.edit.animations.list || []
        if (!error.length && list.length)
            state.selectedAnimation = { objectId: list[list.length - 1].objectId,
                                        order: list[list.length - 1].order }
    }
    /// The Build pane's drag to reorder.
    readonly property ReorderDrag buildDrag: ReorderDrag {
        count: (state.animationList || []).length
        rowHeight: Style.groupRowH + 12
        onMoved: function (from, to) {
            if (!state.edit)
                return
            const error = state.edit.animations.move(from, to)
            state.report(error)
            // An animation's order is its place in the slide's list: the
            // one picked keeps its pick at its new place.
            const pick = state.selectedAnimation
            if (pick && !error.length) {
                const at = pick.order - 1
                const now = at === from ? to
                          : from < at && at <= to ? at - 1
                          : to <= at && at < from ? at + 1
                          : at
                state.selectedAnimation = { objectId: pick.objectId, order: now + 1 }
            }
        }
    }
    function setAnimationField(field, value) {
        const now = state.currentAnimation
        if (now && state.edit)
            state.report(state.edit.animations.set(now.objectId, now.order, field, value))
    }
    property bool sidebarOpen: true
    /// Sections the owner has folded; the long ones start folded.
    property var sectionClosed: ({ text: true, chords: true, geometry: true })
    /// 0 fits the stage; else stage pixels per canvas pixel.
    property real zoom: 0
    /// The zoom chosen is kept within these, however it is chosen.
    readonly property real minZoom: 0.1
    readonly property real maxZoom: 2.0
    function clampZoom(z) { return Math.max(state.minZoom, Math.min(state.maxZoom, z)) }
    /// Stage pixels per canvas pixel, as the stage lays out.
    property real stageScale: 1
    property bool stageDragging: false
    property string statusMessage: ""
    /// Why the last edit was refused; shown in the footer until the next edit.
    property string errorText: ""
    property string nameDraft: ""
    property bool nameSuppress: false

    signal flattenAsked()
    signal slideThemeAsked()
    signal slideBgColorAsked()
    signal slideBgImageAsked()
    signal slideBgVideoAsked()
    /// Choose a file from disk for ``forWhat``, then importMediaFile it:
    /// object | video (a new picture or video), element | video_file (the
    /// selected picture's or video's file), fill (a picture fill on the
    /// selection), poster (the selected video's poster).
    signal mediaFileAsked(string forWhat)
    signal assetAsked(string slot, bool video)
    signal fieldsAsked()
    signal cueRenameStarted(int row)
    signal keyFocusRequested()
    /// The editor closed (saved or cancelled).
    signal finished()

    readonly property int controlH: Style.controlH
    readonly property CanvasRichEdit richEdit: CanvasRichEdit {
        fonts: state.session ? state.session.fonts : null
    }
    /// The family as slides show it, for what sets a font itself.
    function painted(family) {
        return state.session ? state.session.fonts.painted(String(family || ""), state.session.fonts.revision)
                             : String(family || "")
    }

    // ---- the selection ---------------------------------------------------
    readonly property var sel: state.edit ? state.edit.objects.selection : ({})
    readonly property bool hasSel: !!(state.edit && state.edit.objects.hasSelection)
    readonly property int selCount: state.edit ? state.edit.objects.selectedIds.length : 0
    readonly property bool hasSingleSel: state.selCount === 1
    readonly property bool selLocked: !!(state.edit && state.edit.objects.selectionLocked)
    readonly property bool canGroupSel: !!(state.edit && state.edit.objects.canGroup)
    readonly property bool canUngroupSel: !!(state.edit && state.edit.objects.canUngroup)
    readonly property bool canRaiseSel: !!(state.edit && state.edit.objects.canRaise)
    readonly property bool canLowerSel: !!(state.edit && state.edit.objects.canLower)
    readonly property string selType: state.edit ? state.edit.objects.selectedType : ""
    readonly property string selBinding: state.edit ? String(state.sel.binding) : ""
    readonly property var canvasObjects: state.edit ? state.edit.objects.canvasObjects : []
    readonly property bool editorDirty: !!(state.edit && state.edit.dirty) || state.textEditing
                                        || state.cueInlineRenameIndex >= 0
    readonly property bool hasClockOnSlide: {
        const objs = state.canvasObjects || []
        for (let i = 0; i < objs.length; ++i) {
            if (objs[i] && objs[i].type === "clock")
                return true
        }
        return false
    }

    /// Whether the selected object takes the controls ``cap`` names (runs,
    /// font, fill, ...; theme::object_controls): the sidebar's sections and
    /// the ribbon's groups show by it.
    function selSupports(cap) { return !!(state.edit && state.edit.objects.supports[cap]) }
    /// The span each number of the selection's is held to, by its name in
    /// ``sel``: {min, max}, as C++ clamps it.
    readonly property var selRanges: state.edit ? state.edit.objects.ranges : ({})

    /// An edit's answer: "" clears the footer's error, else it shows there.
    function report(err) {
        state.errorText = String(err || "")
        return !state.errorText.length
    }
    function showStatus(msg) {
        state.statusMessage = String(msg || "")
        state.statusTimer.restart()
    }
    /// One property of the selection, by the table's name.
    function setProp(name, value) {
        if (!state.edit)
            return false
        return state.report(state.edit.objects.setSelectedProperty(name, value))
    }
    function objectById(id) {
        const objs = state.canvasObjects || []
        for (let i = 0; i < objs.length; ++i) {
            if (objs[i] && objs[i].id === id)
                return objs[i]
        }
        return null
    }

    // ---- text editing ----------------------------------------------------
    property bool textEditing: false
    property string textEditId: ""
    property string textEditDraft: ""
    property var editLines: []
    property int editSelStart: 0
    property int editSelEnd: 0
    property int editSelTick: 0
    property string editHtml: ""
    property int editHtmlRev: 0
    property bool editHtmlSuppress: false
    /// A linked box being edited: its cues as linkedEditInfo gave them, with
    /// the chords the edit has moved.
    property var editCues: []
    property int editUpcoming: 0
    property int editChordsRev: 0
    readonly property bool editingChords: state.textEditing && !!state.sel.cue_linked
    /// Room over the lines for chord labels, as the output makes it: only
    /// for a box that shows its chords.
    readonly property bool editChordRoom: state.editingChords && !!state.sel.show_chords

    /// An object's type as the canvas text editor takes a box.
    function boxHtmlStyle(obj) {
        return {
            fontSize: obj.font_size,
            fontColor: obj.font_color,
            fontFamily: obj.font_family,
            lineHeight: state.objectLineHeight(obj),
            letterSpacing: state.objectLetterSpacing(obj),
            lineGap: state.objectLineGap(obj)
        }
    }
    function objectLineHeight(obj) { return Number(obj.line_height) }
    function objectLetterSpacing(obj) {
        const n = Number(obj && obj.letter_spacing)
        return isNaN(n) ? 0 : n
    }
    function objectLineGap(obj) {
        const n = Number(obj && obj.line_gap)
        return isNaN(n) ? 0 : Math.max(0, n)
    }

    function beginTextEdit(obj) {
        if (!obj || obj.type !== "text" || !state.edit)
            return
        if (state.isTheme && obj.binding !== "static")
            return
        state.edit.objects.selectObject(obj.id, false)
        state.textEditId = obj.id
        let src = obj.text || ""
        state.editCues = []
        state.editUpcoming = 0
        if (obj.cue_linked) {
            const info = state.edit.cues.linkedEditInfo()
            src = info.markup || ""
            state.editCues = JSON.parse(JSON.stringify(info.cues || []))
            state.editUpcoming = Number(info.upcoming) || 0
            state.editChordsRev = state.editChordsRev + 1
        }
        state.richEdit.loadMarkup(src, !!obj.bold, !!obj.italic, !!obj.underline)
        if (state.isTheme) {
            const cleaned = state.richEdit.sanitizePlain(state.richEdit.plain)
            if (cleaned !== state.richEdit.plain)
                state.richEdit.reconcile(cleaned)
        }
        state.editLines = state.richEdit.lines
        state.textEditDraft = state.richEdit.plain
        state.editSelStart = 0
        state.editSelEnd = 0
        state.textEditing = true
        state.refreshEditHtml()
        // The raster paints this box with its tokens filled in; QML draws the
        // source over the same rectangle, so the raster drops it.
        if (state.isTheme)
            state.refreshDraft()
    }

    function endTextEdit(save) {
        if (!state.textEditing)
            return
        const id = state.textEditId
        if (save && state.edit)
            state.commitEditLines()
        state.textEditing = false
        state.textEditId = ""
        state.textEditDraft = ""
        state.editCues = []
        state.editUpcoming = 0
        state.editLines = []
        state.editHtml = ""
        state.editSelStart = 0
        state.editSelEnd = 0
        if (state.isTheme)
            state.refreshDraft()
    }

    function commitEditLines() {
        if (!state.edit)
            return
        state.richEdit.reconcile(state.richEdit.sanitizePlain(state.textEditDraft))
        state.editLines = state.richEdit.lines
        if (state.sel.cue_linked) {
            state.report(state.edit.cues.setLinkedEdit(state.richEdit.markup(),
                                                       state.editUpcoming, state.editCues))
            return
        }
        const formatted = state.richEdit.objectFlags()
        state.edit.objects.beginGesture()
        state.report(state.edit.objects.setText(state.textEditId, formatted.text || "",
                                                !!formatted.bold, !!formatted.italic,
                                                !!formatted.underline))
        state.edit.objects.endGesture()
    }

    function applyCanvasTextLayout(editItem) {
        if (!state.edit || !editItem)
            return
        const chords = state.editChordRoom ? state.edit.objects.chordRoom / Number(state.sel.font_size) : 0
        TextEditTools.setLineHeight(editItem, state.objectLineHeight(state.sel) + chords, 0)
    }

    function refreshEditHtml() {
        if (!state.textEditing)
            return
        state.editHtml = state.richEdit.html(state.boxHtmlStyle(state.sel), state.stageScale)
        state.editHtmlRev = state.editHtmlRev + 1
    }

    /// Advanced once a second while a clock is on the slide.
    property int clockTick: 0
    function clockText(obj) {
        // The tick is read as a value, as caretStyle's: a bare ``void`` read
        // can be compiled away, and the time would stand still.
        if (state.clockTick < 0)
            return ""
        return state.edit.objects.clockText(!!obj.clock_24h, !!obj.clock_seconds)
    }

    /// What a box shows on the stage: its text styled, a clock's time.
    function objectRichHtml(obj, stageScale) {
        if (!obj || !state.edit)
            return ""
        const text = obj.type === "clock" ? state.clockText(obj) : (obj.text || "")
        const lines = state.edit.objects.parseRichLines(text, !!obj.bold, !!obj.italic,
                                                        !!obj.underline)
        return state.richEdit.htmlFor(lines, state.boxHtmlStyle(obj), stageScale)
    }

    // ---- chords in a linked box being edited -----------------------------
    // Offsets are a cue's ``at``: a place in its text as shown, line breaks
    // not counted. The text edit holds the cues one after another, with line
    // breaks between (and any the operator added).
    function editCueStart(i) {
        let start = 0
        for (let k = 0; k < i && k < state.editCues.length; ++k) {
            start += (Number(state.editCues[k].length) || 0)
                     + (Number(state.editCues[k + 1] && state.editCues[k + 1].before) || 0)
        }
        return start
    }
    function draftPosOfChar(n) {
        const d = state.textEditDraft
        let seen = 0
        for (let i = 0; i < d.length; ++i) {
            if (d[i] === "\n" || d[i] === "\r")
                continue
            if (seen === n)
                return i
            ++seen
        }
        return d.length
    }
    function charsBefore(pos) {
        const d = state.textEditDraft
        let n = 0
        for (let i = 0; i < pos && i < d.length; ++i) {
            if (d[i] !== "\n" && d[i] !== "\r")
                ++n
        }
        return n
    }
    function chordDraftPos(i, at) {
        const len = Number(state.editCues[i].length) || 0
        const g = state.editCueStart(i) + at
        if (at >= len && len > 0)
            return state.draftPosOfChar(g - 1) + 1
        return state.draftPosOfChar(g)
    }
    function editChordPlaces() {
        // Read as values, as caretStyle's tick: the places follow the chords
        // moved and the text typed.
        if (state.editChordsRev < 0 || state.textEditDraft.length < 0)
            return []
        const out = []
        for (let i = 0; i < state.editCues.length; ++i) {
            const chords = state.editCues[i].chords || []
            for (let k = 0; k < chords.length; ++k)
                out.push({ pos: state.chordDraftPos(i, Number(chords[k].at) || 0),
                           chord: String(chords[k].chord) })
        }
        return out
    }
    function chordSlotAt(selStart, selEnd) {
        const d = state.textEditDraft
        const pos = Math.max(0, Math.min(selStart, selEnd, d.length))
        const g = state.charsBefore(pos)
        const lineEnd = pos >= d.length || d[pos] === "\n" || d[pos] === "\r"
        let start = 0
        for (let i = 0; i < state.editCues.length; ++i) {
            const len = Number(state.editCues[i].length) || 0
            const before = Number(state.editCues[i].before) || 0
            if (i > 0 && g < start + before)
                return { cue: i - 1, at: Number(state.editCues[i - 1].length) || 0 }
            start += before
            if (g < start + len || (lineEnd && g === start + len)) {
                const at = g - start
                if (selEnd !== selStart || at >= len)
                    return { cue: i, at: at }
                let w = pos
                while (w > 0 && !/\s/.test(d[w - 1]))
                    --w
                let slot = Math.max(0, state.charsBefore(w) - start)
                const chords = state.editCues[i].chords || []
                for (let k = 0; k < chords.length; ++k) {
                    const c = Number(chords[k].at)
                    if (c >= slot && c <= at)
                        slot = c
                }
                return { cue: i, at: slot }
            }
            start += len
        }
        return null
    }
    function chordAtSlot(slot) {
        if (!slot)
            return ""
        const chords = state.editCues[slot.cue].chords || []
        for (let k = 0; k < chords.length; ++k) {
            if (Number(chords[k].at) === slot.at)
                return String(chords[k].chord)
        }
        return ""
    }
    readonly property string caretChord: {
        // Read as values, as caretStyle's tick.
        if (!state.editingChords || state.editChordsRev < 0 || state.editSelTick < 0)
            return ""
        return state.chordAtSlot(state.chordSlotAt(state.editSelStart, state.editSelEnd))
    }
    function setCaretChord(name) {
        if (!state.editingChords)
            return
        const slot = state.chordSlotAt(state.editSelStart, state.editSelEnd)
        if (!slot)
            return
        const chord = String(name || "").replace(/[\[\]]/g, "").trim()
        const cues = JSON.parse(JSON.stringify(state.editCues))
        const kept = (cues[slot.cue].chords || []).filter(function (c) {
            return Number(c.at) !== slot.at
        })
        if (chord.length)
            kept.push({ at: slot.at, chord: chord })
        kept.sort(function (a, b) { return Number(a.at) - Number(b.at) })
        cues[slot.cue].chords = kept
        state.editCues = cues
        state.editChordsRev = state.editChordsRev + 1
    }

    // ---- type, where the operator means it -------------------------------
    function styleAtOffset(offset) {
        return state.richEdit.styleAt(offset, state.sel.font_size, String(state.sel.font_color),
                                      String(state.sel.font_family))
    }
    function styleAtSelectionStart() {
        return state.styleAtOffset(Math.min(state.editSelStart, state.editSelEnd))
    }
    /// The type at the start of the typed range while text is open, read
    /// once for all the controls that show it; empty otherwise.
    readonly property var caretStyle: {
        // The tick is read as a value: a bare ``void`` read can be compiled
        // away, and the style would not follow the edits.
        if (!state.textEditing || state.editSelTick < 0)
            return ({})
        return state.styleAtSelectionStart()
    }
    function applySelectionFormat(attrs) {
        const a = Math.min(state.editSelStart, state.editSelEnd)
        const b = Math.max(state.editSelStart, state.editSelEnd)
        if (b <= a)
            return false
        state.richEdit.reconcile(state.richEdit.sanitizePlain(state.textEditDraft))
        state.textEditDraft = state.richEdit.plain
        if (!state.richEdit.applySelection(a, b, attrs))
            return false
        state.editLines = state.richEdit.lines
        state.editSelTick = state.editSelTick + 1
        state.refreshEditHtml()
        return true
    }
    function applySelectionFontSize(absPx) {
        const fs = Math.max(8, Number(state.sel.font_size))
        const scalePct = Math.max(10, Math.min(400, Math.round((Math.max(8, absPx) / fs) * 100)))
        return state.applySelectionFormat({ scalePct: scalePct })
    }
    // ---- a paragraph's own settings, while its text is open; otherwise the
    // box's. ``name`` is the box's property.
    readonly property var paragraphKeys: ({
        align: "align", line_height: "lineHeight", space_before: "before",
        space_after: "after", indent: "indent"
    })
    /// What the caret's paragraph has, while text is open; else ``boxValue``.
    function paragraphValue(name, boxValue) {
        if (!state.textEditing)
            return boxValue
        const own = state.caretStyle[state.paragraphKeys[name]]
        return (name === "align" ? String(own).length > 0 : Number(own) >= 0) ? own : boxValue
    }
    /// ``name`` on the paragraphs the selection touches, while text is open;
    /// else on the box.
    function setParagraphProp(name, value) {
        if (!state.textEditing)
            return state.setProp(name, value)
        const attrs = {}
        attrs[state.paragraphKeys[name]] = value
        state.richEdit.reconcile(state.richEdit.sanitizePlain(state.textEditDraft))
        state.textEditDraft = state.richEdit.plain
        state.richEdit.applyParagraph(state.editSelStart, state.editSelEnd, attrs, Number(state.sel.font_size))
        state.editLines = state.richEdit.lines
        state.editSelTick = state.editSelTick + 1
        state.refreshEditHtml()
        return true
    }
    function applyAllFormat(attrs) {
        state.richEdit.reconcile(state.richEdit.sanitizePlain(state.textEditDraft))
        state.richEdit.applyAll(attrs)
        state.textEditDraft = state.richEdit.plain
        state.editLines = state.richEdit.lines
        state.editSelTick = state.editSelTick + 1
        state.refreshEditHtml()
    }
    /// Whether ``key`` (bold, italic, underline) is on: at the start of the
    /// typed range while text is open, else on the object.
    function flagOn(key) {
        if (state.textEditing)
            return !!state.caretStyle[key]
        return !!state.sel[key]
    }
    function superOn() {
        if (state.textEditing)
            return (state.caretStyle.baselineRise || 0) > 0
        return false
    }
    function fontSizeShown() {
        if (state.textEditing && state.editSelEnd > state.editSelStart)
            return state.caretStyle.size
        return state.sel.font_size
    }
    function fontColorShown() {
        if (state.textEditing)
            return state.caretStyle.color
        return state.sel.font_color
    }
    function fontFamilyShown() {
        if (state.textEditing && state.editSelEnd > state.editSelStart)
            return state.caretStyle.family
        return state.sel.font_family
    }
    /// A text style where the operator means it: the typed range while text
    /// is open, all of the text when only the caret is in it, else the object
    /// through ``stored`` (applyStoredFormat when none is given).
    function applyTextStyle(attrs, stored) {
        if (state.textEditing && state.editSelEnd > state.editSelStart)
            state.applySelectionFormat(attrs)
        else if (state.textEditing)
            state.applyAllFormat(attrs)
        else if (stored)
            stored()
        else
            state.applyStoredFormat(attrs)
    }
    function toggleFlag(key) {
        const attrs = {}
        attrs[key] = !state.flagOn(key)
        state.applyTextStyle(attrs)
    }
    function toggleSuper() {
        state.applyTextStyle({ baselineRise: state.superOn() ? 0 : 40 })
    }
    function applyStoredFormat(attrs) {
        if (!state.edit)
            return
        const linked = !!state.sel.cue_linked
        if (state.selType !== "text" || (linked && attrs.baselineRise === undefined)) {
            if (attrs.bold !== undefined)
                state.setProp("bold", !!attrs.bold)
            if (attrs.italic !== undefined)
                state.setProp("italic", !!attrs.italic)
            if (attrs.underline !== undefined)
                state.setProp("underline", !!attrs.underline)
            return
        }
        if (state.isTheme) {
            state.report(state.edit.objects.applyTextAttrs(attrs))
            return
        }
        const src = linked ? state.edit.cues.linkedEditMarkup() : (state.sel.text || "")
        state.richEdit.loadMarkup(src, !!state.sel.bold, !!state.sel.italic, !!state.sel.underline)
        state.richEdit.applyAll(attrs)
        if (linked) {
            state.report(state.edit.cues.setLinkedStyle(state.richEdit.markup()))
            return
        }
        const formatted = state.richEdit.objectFlags()
        state.edit.objects.beginGesture()
        state.report(state.edit.objects.setText(String(state.sel.id), formatted.text || "",
                                                !!formatted.bold, !!formatted.italic,
                                                !!formatted.underline))
        state.edit.objects.endGesture()
    }
    function commitFontSize(n) {
        const px = Math.max(8, Math.round(Number(n)))
        if (state.textEditing && state.editSelEnd > state.editSelStart) {
            state.applySelectionFontSize(px)
            return
        }
        state.setProp("font_size", px)
        if (state.textEditing)
            state.refreshEditHtml()
    }
    function commitFontColor(v) {
        state.applyTextStyle({ color: v }, function () { state.setProp("font_color", v) })
        if (state.textEditing)
            state.refreshEditHtml()
    }
    function commitFontFamily(v) {
        state.applyTextStyle({ fontFamily: v }, function () { state.setProp("font_family", v) })
        if (state.textEditing)
            state.refreshEditHtml()
    }

    // ---- inserting -------------------------------------------------------
    /// The drawing tool in use on the stage: line | polyline |
    /// curve; "" when none.
    property string drawTool: ""
    function startDrawing(tool) {
        state.endTextEdit(true)
        state.drawTool = String(tool || "")
        if (!state.drawTool.length)
            state.keyFocusRequested()
    }
    /// The stage's drawing done: ``points`` [x, y] in canvas px, joined back
    /// to the first when ``closed``.
    function finishDrawing(points, closed) {
        const tool = state.drawTool
        state.drawTool = ""
        state.keyFocusRequested()
        if (state.edit && points && points.length >= 2)
            state.report(state.edit.objects.draw(points, tool === "curve", !!closed))
    }
    /// Insert a kind: text | image | shape | indicator | clock, or a theme's
    /// insertKinds id (lyrics, bible_text, countdown, video_area, ...).
    function insert(kindId, shapeKind) {
        if (!state.edit)
            return
        state.endTextEdit(true)
        let type = kindId
        let binding = "static"
        if (state.isTextTheme) {
            const kinds = state.edit.theme.insertKinds || []
            for (let i = 0; i < kinds.length; ++i) {
                if (kinds[i].id === kindId) {
                    type = kinds[i].type
                    binding = kinds[i].binding
                }
            }
        }
        // A picture of its own is placed once its file is chosen and in the
        // library, so a chooser cancelled leaves nothing behind.
        if (type === "image" && binding === "static") {
            state.mediaFileAsked("object")
            return
        }
        if (type === "video") {
            state.mediaFileAsked("video")
            return
        }
        const options = { binding: binding }
        if (shapeKind)
            options.shapeKind = String(shapeKind)
        if (!state.report(state.edit.objects.insert(type, options)))
            return
        const obj = state.objectById(String(state.sel.id))
        if (obj && obj.type === "text" && binding === "static")
            state.beginTextEdit(obj)
    }
    function insertVideo(filename) {
        if (state.edit)
            state.report(state.edit.objects.insert("video", { filename: filename }))
    }
    function insertImage(filename) {
        if (!state.edit)
            return
        // A theme's insert takes no file: the element is placed, then given it.
        if (state.isTextTheme) {
            if (state.report(state.edit.objects.insert("image", { binding: "static" })))
                state.setProp("filename", filename)
        } else {
            state.report(state.edit.objects.insert("image", { filename: filename }))
        }
    }
    function deleteSelected() {
        if (!state.hasSel || !state.edit)
            return
        state.endTextEdit(false)
        state.report(state.edit.objects.deleteSelected())
    }

    // ---- slides, cues, fields --------------------------------------------
    property int cueInlineRenameIndex: -1
    property bool cueInlineRenameFocused: false
    property string cueInlineRenameDraft: ""
    /// The imports under way, by request id: what each file is for.
    property var pendingImports: ({})
    /// Imported files waiting for a text edit or a drag on the stage to end
    /// before they change the selection.
    property var heldMedia: []

    function toggleLineControl() {
        if (!state.edit || !state.isFreeform)
            return
        if (state.edit.cues.lineControl) {
            if (!state.edit.cues.canDisableLineControl)
                state.flattenAsked()
            else
                state.report(state.edit.cues.setLineControl(false))
        } else {
            state.report(state.edit.cues.setLineControl(true))
        }
    }
    function applySlideBgKind(kind) {
        if (!state.edit)
            return
        if (kind === "color") {
            state.report(state.edit.slides.setBackground("color", ""))
            state.slideBgColorAsked()
            return
        }
        if (kind === "image") {
            state.slideBgImageAsked()
            return
        }
        if (kind === "video") {
            state.slideBgVideoAsked()
            return
        }
        state.report(state.edit.slides.setBackground("none", ""))
    }
    /// A file from disk as the slide's background: into the library first.
    function importSlideBackground(url, video) {
        state.importMediaFile(url, video ? "background_video" : "background_image")
    }
    /// Whether a file for ``use`` (as mediaFileAsked's) is a video.
    function mediaUseIsVideo(use) {
        return use === "video" || use === "video_file" || use === "background_video"
    }
    /// The property of the selection a file for ``use`` sets; "" when it
    /// makes something new instead.
    function mediaUseProperty(use) {
        switch (use) {
        case "element":
        case "video_file":
            return "filename"
        case "fill":
            return "fill_picture_file"
        case "poster":
            return "video_poster"
        default:
            return ""
        }
    }
    /// A file from disk, into the library first, then used as ``use`` says:
    /// background_image | background_video (the slide's background), or as
    /// mediaFileAsked's. What sets a property sets it on the object selected
    /// now. Several may be under way at once.
    function importMediaFile(url, use) {
        if (!state.session || !state.edit || !url)
            return
        const objectId = state.mediaUseProperty(use).length ? String(state.sel.id || "") : ""
        if (state.mediaUseProperty(use).length && !objectId.length) {
            state.showStatus(qsTr("Select the object the file is for."))
            return
        }
        const video = state.mediaUseIsVideo(use)
        const id = state.session.media.importMedia(video ? MediaKind.Video : MediaKind.Image, [String(url)], -1)
        state.pendingImports[String(id)] = {
            use: String(use),
            video: video,
            slideId: state.edit.slides.currentId,
            objectId: objectId
        }
    }
    function finishMediaImport(requestId, result) {
        const key = String(requestId)
        const pending = state.pendingImports[key]
        if (!pending)
            return
        delete state.pendingImports[key]
        const names = (result && result.filenames) ? result.filenames : []
        if (names && names.length) {
            // The file is in the library either way; it is used only if the
            // operator is still on the slide it was chosen for.
            if (state.active && state.edit.slides.currentId === pending.slideId)
                state.useImportedMedia(pending, String(names[0]))
            return
        }
        const errs = (result && result.errors) ? result.errors : []
        state.showStatus(errs && errs.length ? String(errs[0])
                         : pending.video ? qsTr("Could not import that video.")
                         : qsTr("Could not import that image."))
    }
    /// Typing or a drag on the stage is under way: what changes the
    /// selection waits for it to end.
    readonly property bool mediaHeld: state.textEditing || state.stageDragging
    onMediaHeldChanged: state.useHeldMedia()
    function useHeldMedia() {
        if (state.mediaHeld || !state.heldMedia.length)
            return
        const held = state.heldMedia
        state.heldMedia = []
        for (let i = 0; i < held.length; ++i) {
            if (state.active && state.edit.slides.currentId === held[i].pending.slideId)
                state.useImportedMedia(held[i].pending, held[i].filename)
        }
    }
    function useImportedMedia(pending, filename) {
        switch (pending.use) {
        case "background_image":
            state.report(state.edit.slides.setBackground("image", filename))
            return
        case "background_video":
            state.report(state.edit.slides.setBackground("video", filename))
            return
        }
        if (state.mediaHeld) {
            state.heldMedia = state.heldMedia.concat([{ pending: pending, filename: filename }])
            return
        }
        if (pending.use === "video") {
            state.insertVideo(filename)
            return
        }
        if (pending.use === "object") {
            state.insertImage(filename)
            return
        }
        const prop = state.mediaUseProperty(pending.use)
        // The object it was chosen for, if it is still there; what is
        // selected now is selected again after.
        if (!prop.length || !pending.objectId.length || !state.objectById(pending.objectId))
            return
        // A copy: the list read from the session reads it again when used.
        const before = Array.from(state.edit.objects.selectedIds || [], String)
        const elsewhere = before.length !== 1 || String(before[0]) !== pending.objectId
        if (elsewhere)
            state.edit.objects.selectObject(pending.objectId)
        state.setProp(prop, filename)
        if (elsewhere)
            state.edit.objects.selectIds(before)
    }
    function beginCueInlineRename(cueIndex) {
        if (!state.edit || cueIndex < 0)
            return
        state.endTextEdit(true)
        state.edit.cues.select(cueIndex)
        state.cueInlineRenameDraft = String(state.edit.cues.current.text || "")
        state.cueInlineRenameIndex = cueIndex
        state.cueInlineRenameFocused = true
        state.cueRenameStarted(cueIndex)
    }
    function commitCueInlineRename() {
        if (state.cueInlineRenameIndex < 0)
            return
        const text = state.cueInlineRenameDraft
        const index = state.cueInlineRenameIndex
        state.cueInlineRenameIndex = -1
        state.cueInlineRenameFocused = false
        if (!state.edit)
            return
        if (state.edit.cues.index !== index)
            state.edit.cues.select(index)
        state.report(state.edit.cues.rename(text))
    }
    function cancelCueInlineRename() {
        state.cueInlineRenameIndex = -1
        state.cueInlineRenameFocused = false
        state.cueInlineRenameDraft = ""
    }
    /// Move slide ``from`` to sit after cue ``lastCue``. True when the move
    /// was made (or refused, with the reason reported).
    function placeSlideThumb(from, lastCue) {
        if (!state.edit || from < 0 || lastCue < 0)
            return false
        state.report(state.edit.slides.placeThumb(from, lastCue))
        return true
    }
    function reorderSlide(from, insertBefore) {
        if (!state.edit)
            return false
        const n = state.edit.slides.count
        const to = Reorder.landing(from, Math.max(0, Math.min(n, insertBefore)), n)
        if (to < 0)
            return false
        state.report(state.edit.slides.reorder(from, to))
        return true
    }

    // ---- a theme ---------------------------------------------------------
    readonly property var settings: state.edit && state.isTheme ? state.edit.theme.settings : ({})
    /// The text theme context being designed (song, bible, image, video, web,
    /// live, titles); empty elsewhere.
    readonly property string themeContext: state.edit && state.isTextTheme ? String(state.edit.context) : ""
    /// The context's own transition ({type, ms}); Titles shows the Song's.
    readonly property var contextTransition: {
        const all = state.settings.transitions || {}
        const key = state.themeContext === "titles" ? "song" : state.themeContext
        return all[key] || ({})
    }
    /// The span each number in ``settings`` is held to, by its key.
    readonly property var settingRanges: state.edit && state.isTheme ? state.edit.theme.settingRanges : ({})
    function setting(key) { return state.settings[key] }
    function setSetting(key, value) {
        if (state.edit && state.isTheme)
            state.edit.theme.setSetting(key, value)
    }
    /// Paint the draft again; the box being typed into is left to QML.
    function refreshDraft() {
        if (state.edit && state.isTheme)
            state.edit.theme.refreshDraft(state.textEditing ? state.textEditId : "")
    }
    readonly property int draftRevision: state.session ? state.session.themes.draftRevision : 0
    /// The tint over a selected stand-in on a theme's stage: a media area, or
    /// a picture not chosen yet. The raster paints the stand-in itself, at its
    /// place in the stack; this is chrome that says what kind of box it is.
    function stageStandInTint(el) {
        if (!el)
            return "transparent"
        if (el.binding === "video_area")
            return Style.placeholderVideo
        if (el.binding === "image_area")
            return Style.placeholderImage
        if (el.binding === "web_area")
            return Style.placeholderWeb
        if (el.type === "image" && !(el.filename && el.filename.length))
            return Style.placeholderImage
        return "transparent"
    }
    /// A picture's or video's name as the library shows it; files are
    /// stored under names of their own.
    function mediaName(video, filename) {
        const file = String(filename || "")
        if (!file.length || !state.session)
            return file
        return state.session.media.mediaFileDisplayName(video ? MediaKind.Video : MediaKind.Image, file)
    }

    // ---- lifecycle -------------------------------------------------------
    function openFreeform(id) {
        if (!state.edit || !state.edit.openFreeform(id))
            return false
        state.begin()
        return true
    }
    function openTheme(output, kind, themeId) {
        if (!state.edit || !state.edit.openTheme(output, kind, themeId))
            return false
        state.begin()
        return true
    }
    function begin() {
        state.ribbonTab = state.isBgTheme ? "design" : "home"
        state.sidebarTab = "properties"
        state.selectedAnimation = null
        state.drawTool = ""
        state.errorText = ""
        state.statusMessage = ""
        state.nameDraft = state.edit.name
        state.zoom = 0
    }
    /// The window went away (saved, cancelled, or closed from the session):
    /// nothing in progress outlives it, so an import that lands later never
    /// reaches whatever is open by then.
    function end() {
        // Dropped first: the edit ending would otherwise use what it held.
        state.pendingImports = {}
        state.heldMedia = []
        state.endTextEdit(false)
        state.cancelCueInlineRename()
    }
    /// Put away edits in progress; typing is kept.
    function closeEdits() {
        state.cancelCueInlineRename()
        state.endTextEdit(true)
    }
    function cancel() {
        // Dropped first: the edit ending would otherwise use what it held.
        state.pendingImports = {}
        state.heldMedia = []
        state.endTextEdit(false)
        state.cancelCueInlineRename()
        if (state.edit)
            state.edit.cancel()
        state.finished()
    }
    /// Save, typing and a cue rename in progress included. False, with the
    /// reason in the footer, when it was refused.
    function save() {
        state.endTextEdit(true)
        state.commitCueInlineRename()
        if (!state.edit)
            return false
        if (!state.report(state.edit.save()))
            return false
        state.pendingImports = {}
        state.heldMedia = []
        state.finished()
        return true
    }
    readonly property Connections editorWatch: Connections {
        target: state.edit
        function onDocumentChanged() {
            if (state.textEditing)
                state.refreshEditHtml()
            if (state.cueInlineRenameIndex >= 0 && (!state.edit || !state.edit.cues.lineControl))
                state.cancelCueInlineRename()
            if (!state.nameSuppress && state.edit)
                state.nameDraft = state.edit.name
            if (state.isTheme && !state.textEditing && !state.stageDragging)
                state.refreshDraft()
        }
    }
    readonly property Connections mediaWatch: Connections {
        target: state.session ? state.session.media : null
        function onImportFinished(requestId, result) {
            state.finishMediaImport(requestId, result)
        }
    }
    readonly property Timer statusTimer: Timer {
        interval: 2400
        onTriggered: state.statusMessage = ""
    }
    readonly property Timer clockTimer: Timer {
        interval: 1000
        running: state.active && state.hasClockOnSlide
        repeat: true
        onTriggered: state.clockTick = state.clockTick + 1
    }
}
