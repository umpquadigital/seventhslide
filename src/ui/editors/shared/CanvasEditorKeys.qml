import QtQuick
import SeventhSlide

// The slide editor's canvas keyboard shortcuts, applied to the canvas
// session ``editor`` (session.slideEdit.objects). The window keeps its own
// keys (Escape, its browsers) and forwards the rest here:
//
//     Keys.onPressed: function (event) { if (canvasKeys.handle(event)) event.accepted = true }
//
// Delete / Backspace, Undo, Redo, Copy, Cut, Paste, Select All, the arrows
// (Shift: ten pixels), Ctrl+G / Ctrl+Shift+G, Ctrl+L, and Ctrl+] / Ctrl+[
// (Shift: all the way), as in PowerPoint.
QtObject {
    id: keys

    /// The editor session the keys drive; null turns them off.
    property var editor: null
    /// False while a box is being typed into, or a name renamed: the keys
    /// belong to the text field then.
    property bool active: true
    /// What Copy does with nothing selected, or null for nothing (the Freeform
    /// editor copies the slides picked). Returns the editor's error string.
    property var copyWithoutSelection: null
    /// What to say when that copy went through.
    property var copiedWithoutSelection: function () { return qsTr("Slide copied to clipboard") }

    /// A command was refused; ``message`` says why.
    signal refused(string message)
    /// A clipboard command went through; ``message`` says what it did.
    signal done(string message)

    readonly property bool hasSelection: !!(keys.editor && keys.editor.selectedIds.length)

    /// Apply ``event`` if it is one of the shared shortcuts. True when it was.
    function handle(event) {
        if (!keys.editor || !keys.active)
            return false
        const ctrl = !!(event.modifiers & Qt.ControlModifier)
        const shift = !!(event.modifiers & Qt.ShiftModifier)

        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            if (!keys.hasSelection)
                return false
            keys.run(keys.editor.deleteSelected())
            return true
        }
        if (event.matches(StandardKey.Undo)) {
            keys.editor.undo()
            return true
        }
        if (event.matches(StandardKey.Redo)) {
            keys.editor.redo()
            return true
        }
        if (event.matches(StandardKey.Copy)) {
            if (keys.hasSelection)
                keys.clipboard(keys.editor.copySelected(), qsTr("Copied to clipboard"))
            else if (keys.copyWithoutSelection)
                keys.clipboard(keys.copyWithoutSelection(), keys.copiedWithoutSelection())
            return true
        }
        if (event.matches(StandardKey.Cut)) {
            if (!keys.hasSelection)
                return false
            const err = keys.editor.copySelected()
            if (err.length) {
                keys.refused(err)
                return true
            }
            keys.clipboard(keys.editor.deleteSelected(), qsTr("Cut to clipboard"))
            return true
        }
        if (event.matches(StandardKey.Paste)) {
            if (keys.editor.canPaste)
                keys.clipboard(keys.editor.paste(), qsTr("Pasted"))
            return true
        }
        if (event.matches(StandardKey.SelectAll)) {
            keys.editor.selectAll()
            return true
        }
        if (!keys.hasSelection)
            return false

        const step = shift ? 10 : 1
        let dx = 0
        let dy = 0
        if (event.key === Qt.Key_Left)
            dx = -step
        else if (event.key === Qt.Key_Right)
            dx = step
        else if (event.key === Qt.Key_Up)
            dy = -step
        else if (event.key === Qt.Key_Down)
            dy = step
        if (dx !== 0 || dy !== 0) {
            // A locked selection stays put without a message for every key repeat.
            if (!keys.editor.selectionLocked)
                keys.run(keys.editor.nudge(dx, dy))
            return true
        }
        if (ctrl && event.key === Qt.Key_G) {
            keys.run(shift ? keys.editor.ungroup() : keys.editor.group())
            return true
        }
        if (ctrl && event.key === Qt.Key_L) {
            keys.run(keys.editor.setLocked(!keys.editor.selectionLocked))
            return true
        }
        const order = keys.stackingOp(event)
        if (order >= 0) {
            keys.run(keys.editor.reorderSelected(order))
            return true
        }
        return false
    }

    /// The CanvasOrder ``event`` asks for, or -1 when it is not a stacking
    /// shortcut. Shifted brackets arrive as braces on some layouts, so both
    /// codes are accepted.
    function stackingOp(event): int {
        if (!(event.modifiers & Qt.ControlModifier))
            return -1
        const up = event.key === Qt.Key_BracketRight || event.key === Qt.Key_BraceRight
        const down = event.key === Qt.Key_BracketLeft || event.key === Qt.Key_BraceLeft
        if (!up && !down)
            return -1
        const all = !!(event.modifiers & Qt.ShiftModifier)
        if (up)
            return all ? CanvasOrder.Front : CanvasOrder.Forward
        return all ? CanvasOrder.Back : CanvasOrder.Backward
    }

    function run(err) {
        if (err.length)
            keys.refused(err)
    }

    function clipboard(err, message) {
        if (err.length)
            keys.refused(err)
        else
            keys.done(message)
    }
}
