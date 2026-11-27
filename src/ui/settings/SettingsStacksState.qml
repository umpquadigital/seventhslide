import QtQuick
import SeventhSlide
import "../js/Reorder.js" as Reorder

// The Settings dialog's Stacks page: the stacks front to back, what each one
// receives, its name and icon, whether it has a Preview on this computer,
// adding, reordering and deleting them. Every change persists at once.
//
// The dialog shows what this asks for: ``reported`` errors in its footer and
// a confirm for ``deleteAsked`` and ``previewOffAsked``.
QtObject {
    id: state

    property SessionFacade session

    /// The stack picked in the list, or "". What Delete and the arrows act
    /// on is ``shownStackId``: this one while it is listed, else the front one.
    property string selectedStackId: ""

    /// A result to show: an error, or "" to clear the last one.
    signal reported(string error)
    /// Deleting the open stack needs the operator's word; ``message`` says
    /// what goes. On yes, call performDeleteStack(selectedStackId).
    signal deleteAsked(string message)
    /// Taking away a Preview with something cued drops it, so it needs the
    /// operator's word; ``message`` says what goes. On yes, call
    /// performPreviewOff(selectedStackId).
    signal previewOffAsked(string message)

    /// The content types a stack can receive, with their glyphs.
    readonly property var stackTypeItems: ContentTypes.items

    // Core stores stacks back-to-front; the list reads front-to-back (top draws
    // on top), so every display index is the mirror of a core index.
    readonly property var stackRows: {
        const list = state.session ? (state.session.stacks.all || []) : []
        const out = []
        for (let i = list.length - 1; i >= 0; --i)
            out.push(list[i])
        return out
    }

    readonly property var typeDefaults: state.session ? state.session.stacks.typeDefaults : ({})
    /// Where a type goes when no other stack takes it.
    readonly property string fallbackId: state.session ? state.session.stacks.fallbackId : ""

    readonly property string shownStackId: {
        const rows = state.stackRows
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].id === state.selectedStackId)
                return rows[i].id
        }
        return rows.length ? rows[0].id : ""
    }

    function stackDisplayIndex(id) {
        const rows = state.stackRows
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].id === id)
                return i
        }
        return -1
    }

    function stackName(id) {
        const i = state.stackDisplayIndex(id)
        return i >= 0 ? String(state.stackRows[i].name || id) : ""
    }

    /// The type keys that play on ``stackId``.
    function receivedKeys(stackId) {
        const td = state.typeDefaults || {}
        const out = []
        for (const item of state.stackTypeItems) {
            if (td[item.key] === stackId)
                out.push(item.key)
        }
        return out
    }

    /// The Receives checklist for ``stackId``: { id, label, glyph, locked }
    /// per type. On the fallback stack a type it has cannot be unticked: there
    /// is nowhere else for it to go.
    function receivesModel(stackId) {
        const fallback = stackId === state.fallbackId
        const td = state.typeDefaults || {}
        return state.stackTypeItems.map(function (item) {
            return {
                id: item.key,
                label: item.label,
                glyph: item.icon,
                locked: fallback && td[item.key] === stackId
            }
        })
    }

    /// Tick or untick type ``key`` on ``stackId``: ticking moves it here,
    /// unticking sends it back to the fallback stack.
    function toggleReceives(stackId, key) {
        if (!state.session)
            return
        const td = state.typeDefaults || {}
        state.reported(td[key] === stackId
                       ? state.session.stacks.releaseStackTypeDefault(key, stackId)
                       : state.session.stacks.setStackTypeDefault(key, stackId))
    }

    /// The page opens fresh: no row picked.
    function reset() {
        state.selectedStackId = ""
    }

    function selectStack(id) {
        state.selectedStackId = id
    }

    function addNewStack() {
        if (!state.session)
            return
        const err = state.session.stacks.addStack(qsTr("Stack"), "layers")
        state.reported(err)
        if (err.length)
            return
        // add_stack puts the new stack in front: the top row here. Open it so
        // it can be named.
        const rows = state.stackRows
        if (rows.length)
            state.selectedStackId = rows[0].id
    }

    /// Name stack ``id`` ``value``, when it is a change.
    function renameStack(id, value) {
        const name = String(value || "").trim()
        if (!state.session || !name.length || name === state.stackName(id))
            return
        state.reported(state.session.stacks.renameStack(id, name))
    }

    function setStackIcon(id, icon) {
        if (state.session)
            state.reported(state.session.stacks.setStackIcon(id, icon))
    }

    /// Give stack ``id`` a Preview, or take it away: asked first when
    /// something is cued in it.
    function setPreview(id, on) {
        if (!state.session)
            return
        state.selectedStackId = id
        if (!on && state.session.stacks.previewCued(id)) {
            state.reported("")
            state.previewOffAsked(qsTr("Turn Preview off for “%1”? What is cued in it clears; "
                                       + "what is live stays.").arg(state.stackName(id)))
            return
        }
        state.reported(state.session.stacks.setStackPreview(id, on))
    }

    function performPreviewOff(id) {
        if (state.session && id.length)
            state.reported(state.session.stacks.setStackPreview(id, false))
    }

    function beginDeleteStack(id) {
        if (state.stackDisplayIndex(id) < 0 || state.stackRows.length < 2)
            return
        state.selectedStackId = id
        state.reported("")
        state.deleteAsked(qsTr("Delete the stack “%1”? Anything live on it clears.")
                          .arg(state.stackName(id)))
    }

    function performDeleteStack(id) {
        if (!state.session || !id.length)
            return
        const err = state.session.stacks.removeStack(id)
        state.reported(err)
        if (!err.length && state.selectedStackId === id)
            state.selectedStackId = ""
    }

    /// Whether stack ``id`` can move ``step`` rows (-1 up, toward the front;
    /// 1 down) and stay in the list.
    function canMoveStack(id, step) {
        const from = state.stackDisplayIndex(id)
        const to = from + step
        return from >= 0 && to >= 0 && to < state.stackRows.length
    }

    /// Move stack ``id`` ``step`` rows (-1 up, 1 down). Core wants the
    /// reversed (back-to-front) id list.
    function moveStack(id, step) {
        if (!state.session || !state.canMoveStack(id, step))
            return
        const from = state.stackDisplayIndex(id)
        const rows = Reorder.moved(state.stackRows, from, from + step)
        const ids = []
        for (let i = rows.length - 1; i >= 0; --i)
            ids.push(rows[i].id)
        state.reported(state.session.stacks.reorderStacks(ids))
    }
}
