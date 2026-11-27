import QtQuick
import SeventhSlide

// The Settings dialog's Fonts page: the library's fonts, what it names and has
// none for, and adding and removing them. Every change persists at once.
//
// The dialog shows what this asks for: ``reported`` errors, a confirm for
// ``removeAsked``, a file chooser for ``addAsked``, what files added came to
// for ``resultsShown``, and the editors a use of a missing font is opened in
// (``outputAsked``, ``songAsked``, ``profileAsked``). The slide editor it opens
// itself, as the Output window does.
QtObject {
    id: state

    property SessionFacade session
    /// The page is showing. Nothing is read, or loaded into Qt, until it is.
    property bool active: false
    property string searchText: ""
    /// The family picked in the list, or "". What shows is ``shownFamily``: this
    /// one while the search leaves it, else the first.
    property string selectedFamily: ""
    /// What a yes to ``removeAsked`` takes out: these faces, or else a family.
    property var pendingFilenames: []
    property string pendingFamily: ""

    readonly property FontController fonts: state.session ? state.session.fonts : null
    /// Fonts can be added and removed now.
    readonly property bool canChange: !!state.fonts && !state.session.readOnly && !state.fonts.live
                                      && !state.fonts.adding
    /// There is something selected to remove, and it can go now.
    readonly property bool canRemove: state.canChange && !!state.shownFamily
                                      && state.shownFamily.faces > 0

    signal reported(string error)
    /// Removing needs the operator's word. On yes, call performRemove().
    signal removeAsked(string message)
    /// The operator wants to pick files to add; pass them to addFiles.
    signal addAsked()
    signal resultsShown(var results, string note)
    /// Output ``name``'s settings, to open over Settings.
    signal outputAsked(string name)
    /// Library song ``id``, or with ``item`` the song of service row ``id``, to
    /// open in the song editor over Settings.
    signal songAsked(int id, bool item)
    /// Profile ``id``, to show on the Libraries & Profiles page.
    signal profileAsked(int id)

    /// The rows the search leaves, each with the heading it goes under: the
    /// missing apart from the library's, and no heading when none are missing.
    readonly property var familyRows: {
        if (!state.active || !state.fonts)
            return []
        const rows = state.fonts.rows.filter(function (row) {
            return SearchText.matches(String(row.name), state.searchText)
        })
        const anyMissing = rows.some(function (row) { return row.state === "missing" })
        return rows.map(function (row) {
            const group = row.state === "missing" ? qsTr("Missing")
                        : anyMissing ? qsTr("In this library") : ""
            return Object.assign({ group: group }, row)
        })
    }

    /// The family whose faces show, or null when the search leaves none.
    readonly property var shownFamily: {
        const rows = state.familyRows
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].name === state.selectedFamily)
                return rows[i]
        }
        return rows.length ? rows[0] : null
    }

    /// Its faces (``FontController::faces``).
    readonly property var shownFaces: state.active && state.fonts && state.shownFamily
                                      ? state.fonts.faces(state.shownFamily.name, state.fonts.revision)
                                      : []

    /// ``shownFamily``'s name, and whether the library has no font for it: kept
    /// apart so a search that leaves it showing does not read the library again.
    readonly property string shownName: state.shownFamily ? String(state.shownFamily.name) : ""
    readonly property bool shownMissing: !!state.shownFamily && state.shownFamily.state === "missing"
    /// Where a missing family is used (``FontController::uses``); none for the
    /// library's own.
    readonly property var shownUses: state.active && state.fonts && state.shownMissing
                                     ? state.fonts.uses(state.shownName,
                                                        state.fonts.revision + state.usesRevision)
                                     : []
    /// Goes up when what was opened from here closes: it may name the font no
    /// longer, so the page reads again.
    property int usesRevision: 0

    /// The faces picked among ``shownFaces``, by file name.
    readonly property ListSelection faceSelection: ListSelection {
        keyRole: -1  // keyed by keyOf() below: the model is not a list model
        model: state.shownFaces
        function rowCount() { return state.shownFaces.length }
        function keyOf(row) {
            const face = state.shownFaces[row]
            return face ? String(face.filename) : null
        }
    }
    /// Those of ``shownFaces`` selected, in their order. With none, removing
    /// takes the family.
    readonly property var selectedFaces: state.shownFaces.filter(function (face) {
        return state.faceSelection.contains(String(face.filename))
    })

    onSearchTextChanged: state.faceSelection.clear()

    onActiveChanged: {
        if (!state.fonts)
            return
        if (state.active)
            state.fonts.refresh()
        else
            state.fonts.releasePreviews()
    }

    readonly property Connections addWatch: Connections {
        target: state.fonts
        function onAddFinished(results, note) { state.resultsShown(results, note) }
    }

    /// The page opens fresh: no search, the first family showing.
    function reset() {
        state.searchText = ""
        state.selectedFamily = ""
        state.faceSelection.clear()
    }

    /// A click on a family: it is what is selected, and none of its faces.
    function selectFamily(name) {
        state.faceSelection.clear()
        if (state.selectedFamily === name)
            return
        state.selectedFamily = name
        state.reported("")
    }

    /// Open ``use`` (``FontController::uses``) where it is edited. A service
    /// row other than a song's has no editor of its own: it stays as it is.
    function openUse(use) {
        if (!state.session || !use)
            return
        state.reported("")
        switch (String(use.kind)) {
        case "theme":
            state.session.slideEdit.openTheme(String(use.within), String(use.type), String(use.id))
            break
        case "freeform":
            state.session.slideEdit.openFreeform(Number(use.id))
            break
        case "output":
            state.outputAsked(String(use.name))
            break
        case "song":
            state.songAsked(Number(use.id), false)
            break
        case "service":
            if (String(use.type) === "song")
                state.songAsked(Number(use.id), true)
            break
        case "profile":
            state.profileAsked(Number(use.id))
            break
        }
    }

    /// What was opened from here has closed: read what is missing, and where
    /// it is used, again.
    function reread() {
        if (!state.active || !state.fonts)
            return
        state.fonts.refresh()
        state.usesRevision += 1
    }

    /// The slide editor, opened over Settings, closing.
    readonly property Connections editorWatch: Connections {
        target: state.session ? state.session.slideEdit : null
        function onActiveChanged() {
            if (!state.session.slideEdit.active)
                state.reread()
        }
    }

    function openAdd() {
        state.reported("")
        state.addAsked()
    }

    /// Add the files at ``urls``, as a file chooser gives them.
    function addFiles(urls) {
        if (!state.fonts)
            return
        const paths = []
        for (let i = 0; i < urls.length; ++i)
            paths.push(String(urls[i]))
        state.fonts.addFiles(paths)
    }

    /// What stands in once family ``row`` has gone from the library.
    function _afterFamily(row) {
        if (row.named)
            return qsTr("Something in this library uses it, and will show in %1 until the font "
                        + "is added again.").arg(state.fonts.standIn)
        return qsTr("Nothing in this library names it now.")
    }

    /// Ask to remove what is selected: the faces picked, or the family with none
    /// picked. All of a family's faces go as the family.
    function beginRemoveSelected() {
        const row = state.shownFamily
        if (!state.canRemove)
            return
        state.reported("")
        const faces = state.selectedFaces
        if (faces.length === 0 || faces.length >= row.faces) {
            state.pendingFilenames = []
            state.pendingFamily = row.name
            const text = qsTr("Remove %1 from this library, and from every computer that "
                              + "shares it? %2").arg(row.name).arg(state._afterFamily(row))
            state.removeAsked(state._withArriving(text, row.name, []))
            return
        }
        state.pendingFilenames = faces.map(function (face) { return String(face.filename) })
        state.pendingFamily = ""
        const styles = faces.map(function (face) { return String(face.style) }).join(", ")
        const text = (faces.length === 1
                      ? qsTr("Remove %1 %2 from this library, and from every computer that "
                             + "shares it? Text in that style will be drawn from the family’s "
                             + "other styles.")
                      : qsTr("Remove %1 %2 from this library, and from every computer that "
                             + "shares it? Text in those styles will be drawn from the "
                             + "family’s other styles.")).arg(row.name).arg(styles)
        state.removeAsked(state._withArriving(text, row.name, state.pendingFilenames))
    }

    /// ``text``, and what removing ``family`` (or its faces ``filenames``)
    /// does to files still on their way here.
    function _withArriving(text, family, filenames) {
        const note = state.fonts ? state.fonts.arrivingRemoveNote(family, filenames) : ""
        return note.length ? text + " " + note : text
    }

    function performRemove() {
        if (!state.fonts)
            return
        state.reported(state.pendingFilenames.length ? state.fonts.removeFaces(state.pendingFilenames)
                                                     : state.fonts.removeFamily(state.pendingFamily))
        state.pendingFilenames = []
        state.pendingFamily = ""
        state.faceSelection.clear()
    }
}
