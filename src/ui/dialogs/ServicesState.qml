import QtQuick
import SeventhSlide

// The Services window's view-model: the tags it narrows to, the search, the
// order, the service picked (Open loads it, Settings edits it), making and deleting one.
// A service's title, date, time and tags are Service Settings'.
//
// The window shows what this asks for: ``reported`` errors, a confirm for
// ``deleteAsked``, and a new service's settings on ``created``.
QtObject {
    id: state

    property SessionFacade session
    /// The tags the page narrows to: the services with every one. None, all.
    property var filterTags: []
    property string search: ""
    /// The order: "date" (by day, then time) or "name", which the column
    /// heads set (sortBy); it stays from one showing to the next.
    property string sortKey: "date"
    /// A to Z, or oldest day first; newest first is the default.
    property bool sortAscending: false
    /// The service picked, or -1. Open loads it while the page shows it;
    /// one the page hides gives way to the first it shows.
    property var selectedId: -1

    /// The service picked as the page shows it ({name, date, timeLabel,
    /// tags, isOpen...}: ServiceBrowser's roles), or null when none is or the
    /// tags or search hide it.
    readonly property var selected: {
        state.browser.shownIds  // a change to any service reads it again
        const row = state.browser.service(state.selectedId)
        return row.serviceId === undefined ? null : row
    }
    /// Open has something to load: a selected service the page shows.
    readonly property bool canOpen: state.selected !== null

    /// What the page lists: the services by date, and the tags in use.
    readonly property ServiceBrowser browser: ServiceBrowser {
        services: state.session ? state.session.library.services : null
        filterTags: state.filterTags
        search: state.search
        sortKey: state.sortKey
        sortAscending: state.sortAscending
    }

    // A tag no service has any more is no longer narrowed to. Tags or a
    // search that hide the service picked pick the first shown.
    readonly property Connections _keepPicked: Connections {
        target: state.browser
        function onChanged() {
            const kept = state.filterTags.filter(function (name) {
                return state.browser.tags.some(function (t) {
                    return String(t.name).toLowerCase() === String(name).toLowerCase()
                })
            })
            if (kept.length !== state.filterTags.length) {
                state.filterTags = kept
                return
            }
            const shown = state.browser.shownIds
            if (shown.length && shown.indexOf(state.selectedId) < 0)
                state.selectedId = shown[0]
        }
    }

    /// A result to show: an error, or "" to clear the last one.
    signal reported(string error)
    /// Deleting service ``id`` needs the operator's word. On yes, call
    /// performDelete(id).
    signal deleteAsked(var id, string message)
    /// Service ``id`` was just made and picked, to be named.
    signal created(var id)
    /// The selected service was loaded; the window closes.
    signal opened()

    /// The page opens on every service, with the open one selected, as of
    /// today.
    function reset() {
        state.filterTags = []
        state.search = ""
        state.browser.refresh()
        state.selectedId = state.session ? state.session.openServiceId : -1
        state.reported("")
    }

    /// Tag ``name`` is narrowed to (any case).
    function hasTag(name) {
        const lower = String(name).toLowerCase()
        return state.filterTags.some(function (t) { return String(t).toLowerCase() === lower })
    }

    /// Narrow to tag ``name`` too, or no longer. Only a tag in use is
    /// narrowed to: one no service has would show nothing, and be dropped
    /// again as the page reads it.
    function toggleTag(name) {
        const lower = String(name).toLowerCase()
        if (state.hasTag(name)) {
            state.filterTags = state.filterTags.filter(function (t) {
                return String(t).toLowerCase() !== lower
            })
            return
        }
        const inUse = state.browser.tags.find(function (t) {
            return String(t.name).toLowerCase() === lower
        })
        if (inUse)
            state.filterTags = state.filterTags.concat([String(inUse.name)])
    }

    /// Every service again, whatever its tags.
    function clearTags() {
        state.filterTags = []
    }

    /// Order by ``key``: again, the other way. Names go A to Z first,
    /// dates newest first.
    function sortBy(key) {
        if (state.sortKey === key) {
            state.sortAscending = !state.sortAscending
            return
        }
        state.sortKey = key
        state.sortAscending = key === "name"
    }

    function select(id) {
        if (state.selectedId !== id)
            state.reported("")
        state.selectedId = id
    }

    /// Load the selected service, as Open does.
    function openSelected() {
        if (state.canOpen)
            state.openService(state.selectedId)
    }

    function openService(id) {
        if (!state.session || id < 0)
            return
        if (state.session.library.openService(id))
            state.opened()
    }

    /// A new service, dated now: picked, to be named.
    function createService() {
        if (!state.session || !state.session.ready)
            return
        state.search = ""
        state.filterTags = []
        const id = state.session.library.createService(qsTr("New Service"))
        if (id < 0) {
            state.reported(qsTr("Could not create a service."))
            return
        }
        state.selectedId = id
        state.reported("")
        state.created(id)
    }

    function beginDelete(id, name) {
        if (id < 0)
            return
        state.reported("")
        state.deleteAsked(id, qsTr("Permanently delete the service “%1”?").arg(name))
    }

    function performDelete(id) {
        if (!state.session || id < 0)
            return
        const place = state.browser.shownIds.indexOf(id)
        const err = state.session.library.deleteService(id)
        // The one in its place is picked next, so the details stay full.
        if (state.selectedId === id) {
            const shown = state.browser.shownIds
            state.selectedId = shown.length && place >= 0
                               ? shown[Math.min(place, shown.length - 1)] : -1
        }
        state.reported(err)
    }
}
