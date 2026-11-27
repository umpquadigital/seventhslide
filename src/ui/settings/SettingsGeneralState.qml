import QtQuick
import SeventhSlide
import "../js/LibraryTabs.js" as LibraryTabs
import "../js/Reorder.js" as Reorder

// The Settings dialog's General page: the organization's name, the CCLI
// licence number, the theme priority order and the Library panel's tabs. All
// are saved with the dialog's Save, so they are only loaded and read back here.
QtObject {
    id: state

    required property SessionFacade session

    property string organizationName: ""
    property string ccli: ""
    property var priority: ["content", "service", "global"]

    readonly property var priorityLabels: ({
        content: qsTr("Song / Item"),
        service: qsTr("Service"),
        global: qsTr("Global")
    })

    /// The priority picked; ``shownPriorityIndex`` is its place, or the
    /// first's while none is.
    property string selectedPriority: ""
    readonly property int shownPriorityIndex: Math.max(0, state.priority.indexOf(state.selectedPriority))

    function canMovePriority(step) {
        const to = state.shownPriorityIndex + step
        return to >= 0 && to < state.priority.length
    }

    /// Move the priority picked ``step`` places (-1 up, 1 down).
    function movePriority(step) {
        if (!state.canMovePriority(step))
            return
        const from = state.shownPriorityIndex
        state.selectedPriority = state.priority[from]
        state.priority = Reorder.moved(state.priority, from, from + step)
    }

    /// Every Library tab's id in the order the strip takes them, and those
    /// switched off.
    property var tabOrder: LibraryTabs.arranged([])
    property var hiddenTabs: []

    /// The most tabs the strip shows at once, and the choices for it.
    property int tabsShown: 6
    readonly property var tabsShownOptions: state.tabOrder.map(function (id, i) {
        return { value: i + 1, label: String(i + 1) }
    })

    readonly property ReorderDrag tabDrag: ReorderDrag {
        count: state.tabOrder.length
        rowHeight: Style.groupRowH
        onMoved: function (from, to) {
            state.tabOrder = Reorder.moved(state.tabOrder, from, to)
        }
    }

    function tabShown(id) {
        return state.hiddenTabs.indexOf(id) < 0
    }

    /// Whether tab ``id`` can be switched on or off: the strip keeps one.
    function tabToggleable(id) {
        return !state.tabShown(id) || state.hiddenTabs.length < state.tabOrder.length - 1
    }

    function toggleTab(id) {
        if (!state.tabToggleable(id))
            return
        state.hiddenTabs = state.tabShown(id) ? state.hiddenTabs.concat([id])
                                              : state.hiddenTabs.filter(other => other !== id)
    }

    /// Whether the Library tabs differ from what the panel has.
    function tabsChanged() {
        const library = state.session.library
        const stored = LibraryTabs.arranged(library.tabOrder)
        return JSON.stringify([state.tabOrder, state.hiddenTabs, state.tabsShown])
            !== JSON.stringify([stored, LibraryTabs.hidden(library.hiddenTabs),
                                Math.min(library.tabsShown, stored.length)])
    }

    /// Hand the Library tabs to the panel. Not app settings: they are kept on
    /// this computer (LibraryController).
    function saveTabs() {
        state.session.library.tabOrder = state.tabOrder
        state.session.library.hiddenTabs = state.hiddenTabs
        state.session.library.tabsShown = state.tabsShown
    }

    /// Take the stored values from app settings ``d``.
    function load(d) {
        state.organizationName = d.organizationName || ""
        state.ccli = d.ccliLicenceNumber || ""
        const p = d.themePriority
        state.priority = (p && p.length === 3) ? [p[0], p[1], p[2]]
                                               : ["content", "service", "global"]
        state.tabOrder = LibraryTabs.arranged(state.session.library.tabOrder)
        state.hiddenTabs = LibraryTabs.hidden(state.session.library.hiddenTabs)
        state.tabsShown = Math.min(state.session.library.tabsShown, state.tabOrder.length)
    }
}
