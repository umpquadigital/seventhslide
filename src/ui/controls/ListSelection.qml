import QtQuick
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect

// Which rows of a list are selected, and how the mouse changes that - the
// same rules in every list:
//   click             selects just that row
//   Ctrl+click        adds or removes that row
//   Shift+click       selects from the anchor (the last row clicked) to here
//   Ctrl+Shift+click  adds that range
//   a plain press on a selected row keeps the selection while pressed (it can
//   be dragged) and narrows it to that row on release
//   drag on empty space draws a marquee; with Ctrl it adds to the selection
//
// Rows are named by key (an id, a filename, ...), so a selection survives the
// model resetting under it. A row's key is its ``keyRole`` value; a list whose
// rows need a composite key overrides keyOf(). A row whose key is null (or
// a negative id, or an empty string) cannot be selected.
QtObject {
    id: sel

    /// The rows.
    property var model: null
    /// The model role that names a row, by the model's enum (e.g.
    /// ``SongListModel.SongIdRole``). Required, so a list cannot forget it; a
    /// list that overrides keyOf() sets it to -1.
    required property int keyRole
    /// Keep keys in row order rather than in the order they were picked.
    property bool rowOrder: false
    /// The view a marquee is drawn in (its content coordinates).
    property Flickable view: null
    /// For a tiled view: the item whose children are the tiles. Each tile
    /// has an ``index`` (its row). Null for a ListView, whose rows are found
    /// by position.
    property Item tiles: null

    /// The selected keys.
    property var keys: []
    /// The row last picked: what a one-row action acts on. Always one of
    /// ``keys``; null exactly when nothing is selected.
    property var current: null
    /// Where a Shift range starts.
    property var anchor: null

    /// A plain press landed on a selected row: on release, select only it.
    property var narrowOnRelease: null

    property bool marqueeActive: false
    property bool marqueeAdditive: false
    property real marqueeX1: 0
    property real marqueeY1: 0
    property real marqueeX2: 0
    property real marqueeY2: 0
    property var marqueeBase: []

    /// The keys or current row changed.
    signal changed()

    function rowCount() {
        return sel.model ? sel.model.rowCount() : 0
    }

    /// The key of row ``row``, or null when that row cannot be selected.
    function keyOf(row) {
        if (!sel.model || row < 0 || row >= sel.model.rowCount())
            return null
        const k = sel.model.data(sel.model.index(row, 0), sel.keyRole)
        if (k === undefined || k === null || k === "" || (typeof k === "number" && k < 0))
            return null
        return k
    }

    /// The rows a marquee box (content coordinates of ``view``) covers.
    function rowsInBox(x1, y1, x2, y2) {
        if (!sel.tiles)
            return MultiSelect.listIndexRangeInContent(sel.view, x1, y1, x2, y2)
        const rows = []
        const x = Math.min(x1, x2)
        const y = Math.min(y1, y2)
        const w = Math.abs(x2 - x1)
        const h = Math.abs(y2 - y1)
        const kids = sel.tiles.children
        for (let i = 0; i < kids.length; ++i) {
            const tile = kids[i]
            if (!tile || tile.index === undefined || !tile.visible)
                continue
            const tl = tile.mapToItem(sel.view.contentItem, 0, 0)
            if (MultiSelect.rectsIntersect(x, y, w, h, tl.x, tl.y, tile.width, tile.height))
                rows.push(tile.index)
        }
        return rows
    }

    function rowOfKey(key) {
        if (key === null || key === undefined)
            return -1
        for (let r = 0; r < sel.rowCount(); ++r) {
            if (sel.keyOf(r) === key)
                return r
        }
        return -1
    }

    function contains(key) {
        return key !== null && key !== undefined && sel.keys.indexOf(key) >= 0
    }

    /// Select ``keys`` (duplicates and nulls dropped). ``current`` defaults
    /// to the last key; ``anchor`` is left alone unless given.
    function set(keys, current, anchor) {
        const next = []
        const seen = new Set()
        for (let i = 0; i < keys.length; ++i) {
            const k = keys[i]
            if (k !== null && k !== undefined && !seen.has(k)) {
                seen.add(k)
                next.push(k)
            }
        }
        if (sel.rowOrder) {
            const rank = {}
            for (let r = 0; r < sel.rowCount(); ++r)
                rank[String(sel.keyOf(r))] = r
            next.sort(function (a, b) {
                const ra = rank[String(a)]
                const rb = rank[String(b)]
                return (ra === undefined ? Infinity : ra) - (rb === undefined ? Infinity : rb)
            })
        }
        sel.keys = next
        sel.current = (current !== undefined && current !== null && seen.has(current))
                ? current : (next.length ? next[next.length - 1] : null)
        if (anchor !== undefined)
            sel.anchor = anchor
        sel.changed()
    }

    function clear() {
        sel.narrowOnRelease = null
        sel.set([], null, null)
    }

    function selectOnly(key) {
        sel.set([key], key, key)
    }

    function toggle(key) {
        const next = sel.keys.slice()
        const at = next.indexOf(key)
        if (at >= 0)
            next.splice(at, 1)
        else
            next.push(key)
        sel.set(next, key, key)
    }

    /// Select the rows from the anchor to ``key``; with ``additive``, on top
    /// of what is selected. The anchor stays.
    function extendTo(key, additive) {
        const to = sel.rowOfKey(key)
        if (to < 0)
            return
        let from = sel.rowOfKey(sel.anchor)
        if (from < 0)
            from = to
        const next = additive ? sel.keys.slice() : []
        for (let r = Math.min(from, to); r <= Math.max(from, to); ++r)
            next.push(sel.keyOf(r))
        sel.set(next, key)
    }

    /// A press on the row named ``key``, with the keyboard ``modifiers`` held.
    /// Returns what it did: "range", "toggle", "keep" (a selected row, kept
    /// for a drag) or "select".
    function press(key, modifiers) {
        sel.narrowOnRelease = null
        const toggle = MultiSelect.isToggle(modifiers)
        if (MultiSelect.isRange(modifiers) && sel.rowOfKey(sel.anchor) >= 0) {
            sel.extendTo(key, toggle)
            return "range"
        }
        if (toggle) {
            sel.toggle(key)
            return "toggle"
        }
        if (sel.contains(key)) {
            sel.narrowOnRelease = key
            return "keep"
        }
        sel.selectOnly(key)
        return "select"
    }

    /// The press ended without a drag.
    function release() {
        const key = sel.narrowOnRelease
        sel.narrowOnRelease = null
        if (key !== null && key !== undefined)
            sel.selectOnly(key)
    }

    /// The press became a drag (or was canceled): the selection stays.
    function keepOnRelease() {
        sel.narrowOnRelease = null
    }

    /// Start a marquee at (``x``, ``y``) in ``item``.
    function beginMarquee(item, x, y, additive) {
        if (!sel.view)
            return
        const p = item.mapToItem(sel.view.contentItem, x, y)
        sel.narrowOnRelease = null
        sel.marqueeAdditive = additive
        sel.marqueeBase = additive ? sel.keys.slice() : []
        sel.marqueeX1 = p.x
        sel.marqueeY1 = p.y
        sel.marqueeX2 = p.x
        sel.marqueeY2 = p.y
        sel.marqueeActive = true
        sel.updateMarquee(item, x, y)
    }

    /// The marquee's corner moved to (``x``, ``y``) in ``item``.
    function updateMarquee(item, x, y) {
        if (!sel.marqueeActive || !sel.view)
            return
        const p = item.mapToItem(sel.view.contentItem, x, y)
        sel.marqueeX2 = p.x
        sel.marqueeY2 = p.y
        MultiSelect.autoScrollView(sel.view, item.mapToItem(sel.view, x, y).y)
        const rows = sel.rowsInBox(sel.marqueeX1, sel.marqueeY1, sel.marqueeX2, sel.marqueeY2)
        const next = sel.marqueeBase.slice()
        let last = null
        for (let i = 0; i < rows.length; ++i) {
            const k = sel.keyOf(rows[i])
            if (k === null || k === undefined)
                continue
            next.push(k)
            last = k
        }
        sel.set(next, last, last)
    }

    function endMarquee() {
        sel.marqueeActive = false
        sel.marqueeAdditive = false
    }
}
