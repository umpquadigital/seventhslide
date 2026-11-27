import QtQuick
import SeventhSlide
import "../js/Reorder.js" as Reorder

// Settings > Outputs: the output picked, and adding, ordering, switching and
// deleting outputs. The rows are session.outputs; changing one output is the
// Output window's.
QtObject {
    id: state

    property SessionFacade session
    readonly property OutputListModel outputs: state.session ? state.session.outputs : null

    /// The id of the output picked, which a rename leaves alone; ``shownId``,
    /// ``shownIndex`` and ``shownName`` are that output's, or the first's
    /// while none is.
    property string selectedId: ""
    readonly property var ids: state.outputs ? state.outputs.ids : []
    readonly property int shownIndex: state.ids.length ? Math.max(0, state.ids.indexOf(state.selectedId)) : -1
    readonly property string shownId: state.shownIndex >= 0 ? state.ids[state.shownIndex] : ""
    readonly property string shownName: state.shownIndex >= 0 ? state.outputs.names[state.shownIndex] : ""

    /// Open output ``name``'s settings (the Output window, over Settings).
    signal outputSettingsRequested(string name)
    /// A result to show: an error, or "" to clear the last one.
    signal reported(string error)
    /// Deleting output ``name`` needs the operator's word; on yes, call
    /// performDeleteOutput(name).
    signal deleteAsked(string name, string message)

    function addOutput() {
        if (!state.session)
            return
        const created = state.session.outputSettings.createOutput(qsTr("Output"))
        if (created.error) {
            state.reported(created.error)
            return
        }
        state.reported("")
        const at = state.outputs.names.indexOf(created.name)
        if (at >= 0)
            state.selectedId = state.ids[at]
        state.outputSettingsRequested(created.name)
    }

    function openShown() {
        if (state.shownName.length)
            state.outputSettingsRequested(state.shownName)
    }

    function beginDeleteOutput() {
        if (!state.shownName.length)
            return
        state.reported("")
        state.deleteAsked(state.shownName,
                          (state.ids.length === 1
                           ? qsTr("Permanently delete the output “%1”? It is the last one: "
                                  + "nothing is shown anywhere until there is an output again. "
                                  + "Its themes and destinations go with it.")
                           : qsTr("Permanently delete the output “%1”? Its themes and "
                                  + "destinations go with it. Switching it off instead "
                                  + "keeps them.")).arg(state.shownName))
    }

    function performDeleteOutput(name) {
        if (state.session)
            state.reported(state.session.outputSettings.deleteOutput(name))
    }

    /// Whether the output picked can move ``step`` rows (-1 up, 1 down).
    function canMoveShown(step) {
        const to = state.shownIndex + step
        return state.shownIndex >= 0 && to >= 0 && to < state.ids.length
    }

    /// Move the output picked ``step`` rows (-1 up, 1 down).
    function moveShown(step) {
        if (!state.session || !state.canMoveShown(step))
            return
        const names = state.outputs.names
        const from = state.shownIndex
        state.selectedId = state.shownId
        state.reported(state.session.outputSettings.reorderOutputs(
                           Reorder.moved(names, from, from + step)))
    }

    /// Switch output ``id`` on or off; the last one on stays on, and says so.
    function setOutputOn(id, on) {
        state.selectedId = id
        const at = state.ids.indexOf(id)
        if (state.session && at >= 0)
            state.reported(state.session.outputSettings.setOutputOn(state.outputs.names[at], on))
    }
}
