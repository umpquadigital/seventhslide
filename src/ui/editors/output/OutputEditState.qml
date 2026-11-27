import QtQuick
import SeventhSlide

// The Output editor's view-model: the output's settings and destinations and
// its theme gallery. A theme is designed in the slide editor
// (session.slideEdit); the gallery reloads when that closes. OutputEditDialog
// is the sheet around it and its panels show it.
//
// What only the view can do is asked for, through the ``...Asked`` and
// ``...Requested`` signals (confirms, the gallery's own paging and menus).
QtObject {
    id: state

    property SessionFacade session

    /// Deleting the selected theme needs the operator's word. On yes, call
    /// confirmDeleteTheme.
    signal themeDeleteAsked(string message)
    /// The gallery goes back to the top, unfiltered.
    signal galleryResetRequested()
    /// The gallery closes its menus.
    signal galleryMenusCloseRequested()

    property bool open: false
    property string outputName: ""
    /// The id of the output ``outputName`` names; set as the editor loads it.
    property string outputId: ""
    property string errorText: ""

    property string tab: "general" // general | a theme kind (themeKinds) | destinations

    /// The slide editor closing on a theme: the gallery shows what it saved.
    readonly property Connections slideEditWatch: Connections {
        target: state.session ? state.session.slideEdit : null
        function onDocumentChanged() {
            if (state.open && state.onThemeTab
                    && !state.session.slideEdit.active)
                state.reloadThemes()
        }
    }

    // --- General ---
    readonly property OutputDestinationsState destState: OutputDestinationsState {
        session: state.session
        outputId: state.outputId
        outputName: state.outputName
        canvasW: state.canvasSize.width
        canvasH: state.canvasSize.height
        shown: state.open && state.tab === "destinations"
        onReported: function (error) { state.errorText = error }
    }
    property string editName: ""
    /// The canvas as its fields hold it.
    property string canvasW: "1920"
    property string canvasH: "1080"
    /// The canvas in pixels; 0 for a side its field does not hold a number for.
    readonly property size canvasSize: Qt.size(parseInt(state.canvasW, 10) || 0,
                                               parseInt(state.canvasH, 10) || 0)
    /// The sizes offered by name, by shape: [{heading, presets: [{name, title,
    /// width, height}]}].
    readonly property var canvasPresetGroups: state.session
                                              ? state.session.outputSettings.canvasPresetGroups()
                                              : []
    /// The canvas described: {preset (its preset's title, or empty), aspect,
    /// problem (why it cannot be saved, or empty)}.
    readonly property var canvasInfo: state.session
                                      ? state.session.outputSettings.describeCanvas(
                                            state.canvasSize.width, state.canvasSize.height)
                                      : ({ preset: "", aspect: "", problem: "" })
    property var enabledStackIds: []
    /// The content types this output leaves out (ContentTypes' keys).
    property var disabledContentTypes: []
    property bool exemptBlank: false
    property bool exemptFreeze: false
    /// How an audio item's sound comes and goes on this output, in ms, as
    /// typed: {fade_in_ms, fade_out_ms, overlap_ms}. 0 is a cut.
    property var audioFades: ({ fade_in_ms: "0", fade_out_ms: "0", overlap_ms: "0" })
    /// Whether the transport's Play and Pause fade too.
    property bool audioFadesOnPlayPause: false
    /// The output's glyph (one of outputSettings.iconIds()).
    property string editIcon: "display"

    // --- Themes (the gallery) ---
    property string kind: "song" // one of themeKinds (follows the tab)
    /// The output's theme libraries, a tab each, in order: [{kind, label}].
    readonly property var themeKinds: state.session ? state.session.themes.kinds : []
    /// Whether the open tab is a theme library's gallery.
    readonly property bool onThemeTab: state.isThemeTab(state.tab)
    property string selectedId: ""
    property var themes: []
    property var defaults: ({})

    readonly property int controlH: Math.max(26, Math.ceil(state.controlMetrics.height) + 10)
    readonly property FontMetrics controlMetrics: FontMetrics {
        font.pixelSize: Style.fontUi
    }

    function openFor(name, startTab = "general") {
        if (!state.session || !name)
            return
        state.outputName = name
        state.outputId = ""
        state.errorText = ""
        state.tab = startTab
        if (state.isThemeTab(state.tab))
            state.kind = state.tab
        state.selectedId = ""
        state.resetGalleryBrowse()
        state.loadGeneral()
        if (state.tab === "destinations")
            state.destState.loadDestinations()
        if (state.onThemeTab)
            state.reloadThemes()
        state.open = true
    }

    function close() {
        state.open = false
        state.errorText = ""
    }

    function loadGeneral() {
        const d = state.session.outputSettings.getOutputSettings(state.outputName)
        if (!d || !d.name) {
            state.errorText = qsTr("Output not found")
            return
        }
        state.editName = d.name || ""
        state.canvasW = String(d.canvasWidth)
        state.canvasH = String(d.canvasHeight)
        state.enabledStackIds = d.enabledStackIds || []
        state.disabledContentTypes = d.disabledContentTypes || []
        state.exemptBlank = !!d.exemptFromGlobalBlank
        state.exemptFreeze = !!d.exemptFromGlobalFreeze
        const fades = d.audioFades || {}
        state.audioFades = {
            fade_in_ms: String(fades.fade_in_ms || 0),
            fade_out_ms: String(fades.fade_out_ms || 0),
            overlap_ms: String(fades.overlap_ms || 0)
        }
        state.audioFadesOnPlayPause = !!fades.on_play_pause
        state.editIcon = d.icon
        state.outputName = d.name
        state.outputId = d.id || ""
        state.session.destinations.refreshDisplays()
        state.destState.refreshRuntimes()
    }

    function pickCanvasPreset(width, height) {
        state.canvasW = String(width)
        state.canvasH = String(height)
    }

    /// Landscape to portrait and back: the width is the height.
    function swapCanvasSides() {
        const w = state.canvasW
        state.canvasW = state.canvasH
        state.canvasH = w
    }

    function stackIsEnabled(id) {
        const all = state.enabledStackIds
        if (!all || all.length === 0)
            return true
        return all.indexOf(id) >= 0
    }

    function toggleOutputStack(id) {
        const defs = state.session ? (state.session.stacks.all || []) : []
        const ids = []
        for (let i = 0; i < defs.length; ++i)
            ids.push(defs[i].id)
        let cur = (state.enabledStackIds && state.enabledStackIds.length)
                  ? state.enabledStackIds.slice()
                  : ids.slice()
        const ix = cur.indexOf(id)
        if (ix >= 0)
            cur.splice(ix, 1)
        else
            cur.push(id)
        if (cur.length === ids.length)
            state.enabledStackIds = []
        else
            state.enabledStackIds = cur
    }

    /// One of the audio fades, as typed.
    function setAudioFade(key, text) {
        const next = Object.assign({}, state.audioFades)
        next[key] = text
        state.audioFades = next
    }

    function contentTypeIsEnabled(key) {
        return state.disabledContentTypes.indexOf(key) < 0
    }

    function toggleContentType(key) {
        state.disabledContentTypes = state.contentTypeIsEnabled(key)
                                     ? state.disabledContentTypes.concat(key)
                                     : state.disabledContentTypes.filter(k => k !== key)
    }

    function saveGeneral() {
        if (!state.session)
            return
        const result = state.session.outputSettings.updateOutputSettings(state.outputName, {
            name: state.editName,
            canvasWidth: state.canvasSize.width,
            canvasHeight: state.canvasSize.height,
            enabledStackIds: state.enabledStackIds,
            disabledContentTypes: state.disabledContentTypes,
            exemptFromGlobalBlank: state.exemptBlank,
            exemptFromGlobalFreeze: state.exemptFreeze,
            icon: state.editIcon,
            // Not a number is a cut; the range is the output's to keep.
            audioFades: {
                fade_in_ms: parseInt(state.audioFades.fade_in_ms, 10) || 0,
                fade_out_ms: parseInt(state.audioFades.fade_out_ms, 10) || 0,
                overlap_ms: parseInt(state.audioFades.overlap_ms, 10) || 0,
                on_play_pause: state.audioFadesOnPlayPause
            }
        })
        if (result && result.error) {
            state.errorText = result.error
            return
        }
        const next = (result && result.name) ? result.name : state.outputName
        state.outputName = next
        state.close()
    }

    /// Whether tab ``id`` is one of ``themeKinds``'.
    function isThemeTab(id) {
        return state.themeKinds.some(function (k) { return k.kind === id })
    }

    function switchTab(id) {
        state.errorText = ""
        state.tab = id
        if (id === "destinations")
            state.destState.loadDestinations()
        if (state.isThemeTab(id)) {
            state.kind = id
            state.selectedId = ""
            state.resetGalleryBrowse()
            state.reloadThemes()
        }
    }

    function reloadThemes() {
        if (!state.session || !state.outputName)
            return
        state.themes = state.session.themes.list(state.outputName, state.kind) || []
        state.defaults = state.session.themes.categoryDefaults(state.outputName) || {}
        if (state.selectedId) {
            let found = false
            for (let i = 0; i < state.themes.length; ++i) {
                if (state.themes[i].id === state.selectedId) {
                    found = true
                    break
                }
            }
            if (!found)
                state.selectedId = ""
        }
    }

    function closeGalleryMenus() {
        state.galleryMenusCloseRequested()
    }

    function resetGalleryBrowse() {
        state.galleryResetRequested()
    }

    function selectedThemeName() {
        for (let i = 0; i < state.themes.length; ++i) {
            if (state.themes[i].id === state.selectedId)
                return state.themes[i].name || qsTr("Untitled")
        }
        return qsTr("Untitled")
    }

    function createNew() {
        const created = state.session.themes.create(state.outputName, state.kind, qsTr("Untitled"))
        if (created && created.error) {
            state.errorText = created.error
            return
        }
        state.selectedId = created.id
        state.reloadThemes()
        state.enterDesign(created.id)
    }

    function duplicateSelected() {
        if (!state.selectedId)
            return
        const created = state.session.themes.create(
                    state.outputName, state.kind, "", {}, "", state.selectedId)
        if (created && created.error) {
            state.errorText = created.error
            return
        }
        state.selectedId = created.id
        state.reloadThemes()
    }

    function deleteSelected() {
        if (!state.selectedId)
            return
        state.closeGalleryMenus()
        state.themeDeleteAsked(qsTr("Permanently delete the theme “%1”?")
                               .arg(state.selectedThemeName()))
    }

    function confirmDeleteTheme() {
        const err = state.session.themes.remove(state.outputName, state.kind, state.selectedId)
        if (err.length) {
            state.errorText = err
            return
        }
        state.selectedId = ""
        state.reloadThemes()
    }

    function setDefault(cat, field) {
        if (!state.selectedId)
            return
        const err = state.session.themes.setDefault(
                    state.outputName, cat, field, state.selectedId)
        if (err.length) {
            state.errorText = err
            return
        }
        state.defaults = state.session.themes.categoryDefaults(state.outputName) || {}
        state.closeGalleryMenus()
    }

    function enterDesign(id) {
        const tid = id || state.selectedId
        if (!tid)
            return
        state.selectedId = tid
        state.closeGalleryMenus()
        state.errorText = ""
        if (!state.session.slideEdit.openTheme(state.outputName, state.kind, tid))
            state.errorText = qsTr("Could not open the theme")
    }

}
