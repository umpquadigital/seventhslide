import QtQuick
import SeventhSlide

// The Settings dialog's Bibles page: the translations, searching them, and
// renaming, abbreviating, switching off, deleting and importing one. Every
// change persists at once.
//
// The dialog shows what this asks for: ``reported`` errors, a confirm for
// ``deleteAsked``, and a file chooser for ``importAsked``.
QtObject {
    id: state

    property SessionFacade session
    property string bibleSearchText: ""
    /// The column the list is sorted by, "name" or "abbreviation", and which
    /// way.
    property string bibleSortKey: "name"
    property bool bibleSortAsc: true
    /// The translation picked in the list, or -1. What shows is
    /// ``shownBible``: this one while the search leaves it, else the first.
    property var selectedBibleId: -1
    /// Bumped when the list changes. A name or abbreviation edit changes rows
    /// in place, which the list's ``count`` does not notify, so what reads
    /// the rows' text in a binding reads this too.
    property int _revision: 0

    /// A result to show: an error, or "" to clear the last one.
    signal reported(string error)
    /// Deleting the open translation needs the operator's word. On yes, call
    /// performDeleteBible(selectedBibleId).
    signal deleteAsked(string message)
    /// The operator wants to pick a file to import; pass it to importBible.
    signal importAsked()

    /// Whether a translation with ``name`` and ``abbreviation`` shows under
    /// the search.
    function bibleMatches(name, abbreviation) {
        const q = state.bibleSearchText
        return SearchText.matches(String(name || ""), q)
               || SearchText.matches(String(abbreviation || ""), q)
    }

    /// The translations the search leaves, sorted by ``bibleSortKey``: ``{
    /// id, name, abbreviation, enabled }`` each. Reads the list's ``count``,
    /// so it follows imports and deletes (a ``rowCount()`` call in a binding
    /// would not), and ``_revision`` for edits in place.
    readonly property var bibleRows: {
        const m = state.session ? state.session.library.bibles : null
        if (!m || state._revision < 0)
            return []
        const out = []
        for (let i = 0; i < m.count; ++i) {
            const name = m.nameAt(i)
            const abbreviation = m.abbreviationAt(i)
            if (state.bibleMatches(name, abbreviation))
                out.push({ id: m.idAt(i), name: name, abbreviation: abbreviation,
                           enabled: m.enabledAt(i) })
        }
        const key = state.bibleSortKey === "abbreviation" ? "abbreviation" : "name"
        const sign = state.bibleSortAsc ? 1 : -1
        // Ties (no abbreviation) fall back to the name, always A to Z.
        out.sort(function (a, b) {
            return sign * String(a[key]).localeCompare(String(b[key]))
                   || String(a.name).localeCompare(String(b.name))
        })
        return out
    }

    /// The translation whose settings show: the one picked while the search
    /// leaves it, otherwise the first that is left; null when none is.
    readonly property var shownBible: {
        const rows = state.bibleRows
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].id === state.selectedBibleId)
                return rows[i]
        }
        return rows.length ? rows[0] : null
    }

    /// The list changes in place (an edit) or whole (another library) from
    /// elsewhere too.
    readonly property Connections biblesWatch: Connections {
        target: state.session ? state.session.library.bibles : null
        function onDataChanged() { ++state._revision }
        function onModelReset() { ++state._revision }
    }

    /// Report ``error`` from a change just made, and let bindings re-read.
    function _changed(error) {
        ++state._revision
        state.reported(error)
    }

    function bibleName(id) {
        return state._bibleText(id, false)
    }

    function bibleAbbreviation(id) {
        return state._bibleText(id, true)
    }

    function _bibleText(id, abbreviation) {
        const m = state.session ? state.session.library.bibles : null
        if (!m)
            return ""
        for (let i = 0; i < m.rowCount(); ++i) {
            if (m.idAt(i) === id)
                return abbreviation ? m.abbreviationAt(i) : m.nameAt(i)
        }
        return ""
    }

    /// Sort by column ``key``; by the same one again, the other way.
    function sortBiblesBy(key) {
        if (state.bibleSortKey === key) {
            state.bibleSortAsc = !state.bibleSortAsc
            return
        }
        state.bibleSortKey = key
        state.bibleSortAsc = true
    }

    /// The page opens fresh: no search, the first translation showing.
    function reset() {
        state.bibleSearchText = ""
        state.selectedBibleId = -1
    }

    /// Show translation ``id``'s settings.
    function selectBible(id) {
        if (state.selectedBibleId === id)
            return
        state.selectedBibleId = id
        state.reported("")
    }

    /// Give translation ``id`` the name ``text``, when it is a change.
    function renameBible(id, text) {
        if (!state.session || id < 0)
            return
        const name = String(text || "").trim()
        if (!name.length || name === state.bibleName(id))
            return
        state._changed(state.session.library.renameBible(id, name))
    }

    /// Give translation ``id`` the abbreviation ``text`` ("" for none), when
    /// it is a change.
    function setAbbreviation(id, text) {
        if (!state.session || id < 0)
            return
        const abbreviation = String(text || "").trim()
        if (abbreviation === state.bibleAbbreviation(id))
            return
        state._changed(state.session.library.setBibleAbbreviation(id, abbreviation))
    }

    /// Offer translation ``id`` for lookup, or not.
    function setEnabled(id, on) {
        if (!state.session || id < 0)
            return
        state._changed(state.session.library.setBibleEnabled(id, on))
    }

    function beginDeleteBible(id) {
        if (id < 0)
            return
        state.selectedBibleId = id
        state.reported("")
        state.deleteAsked(qsTr("Permanently delete the translation “%1”?")
                          .arg(state.bibleName(id)))
    }

    function performDeleteBible(id) {
        if (!state.session || id < 0)
            return
        const err = state.session.library.deleteBible(id)
        if (state.selectedBibleId === id)
            state.selectedBibleId = -1
        state._changed(err)
    }

    function openBibleImport() {
        state.importAsked()
    }

    /// Import the bible file at ``url`` and show it.
    function importBible(url) {
        if (!state.session)
            return
        const result = state.session.library.importBible(url)
        if (result && result.error) {
            state.reported(result.error)
            return
        }
        state._changed("")
        if (result && result.id >= 0)
            state.selectedBibleId = result.id
    }
}
