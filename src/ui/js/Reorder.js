.pragma library

// Drag-to-reorder arithmetic every list shares: where a pointer would drop a
// row, and where the row then lands. A slot ``insertBefore`` is in
// [0, count]: before row i, or count for after the last one.

/// The slot under ``y`` in a list of equal rows ``rowHeight`` apart plus
/// ``gap``, ``y`` measured from the first row's top. The upper half of a row
/// drops before it, the lower half after it.
function slotByPitch(count, rowHeight, gap, y) {
    if (count <= 0 || y <= 0)
        return 0
    const pitch = rowHeight + gap
    const i = Math.min(count - 1, Math.floor(y / pitch))
    return y > i * pitch + rowHeight / 2 ? i + 1 : i
}

/// The slot under ``contentY`` in ``view``, a ListView whose rows may differ
/// in height. The spacing between two rows drops between them; below the
/// last row is after it.
function slotInView(view, contentY) {
    if (view.count <= 0 || contentY <= 0)
        return 0
    const x = view.width / 2
    const row = view.itemAt(x, contentY)
    if (!row) {
        const above = view.indexAt(x, contentY - view.spacing)
        return above < 0 ? view.count : above + 1
    }
    const i = view.indexAt(x, contentY)
    return contentY - row.y > row.height / 2 ? i + 1 : i
}

/// The index row ``from`` ends up at when dropped in slot ``insertBefore``,
/// or -1 when it would not move (or either is out of range).
function landing(from, insertBefore, count) {
    if (from < 0 || from >= count || insertBefore < 0 || insertBefore > count)
        return -1
    const to = insertBefore > from ? insertBefore - 1 : insertBefore
    return to === from ? -1 : to
}

/// A copy of ``list`` with its item ``from`` moved to index ``to``.
function moved(list, from, to) {
    const out = list.slice()
    const item = out.splice(from, 1)[0]
    out.splice(to, 0, item)
    return out
}
