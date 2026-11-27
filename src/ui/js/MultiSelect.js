.pragma library

function isToggle(modifiers) {
    return !!(modifiers & Qt.ControlModifier) || !!(modifiers & Qt.MetaModifier)
}

function isRange(modifiers) {
    return !!(modifiers & Qt.ShiftModifier)
}

function idIndex(arr, id) {
    const n = Number(id)
    for (let i = 0; i < arr.length; ++i) {
        if (Number(arr[i]) === n)
            return i
    }
    return -1
}

function containsId(arr, id) {
    return idIndex(arr, id) >= 0
}

function uniqueSortedInts(arr) {
    const nums = []
    const seen = {}
    if (!arr)
        return nums
    for (let i = 0; i < arr.length; ++i) {
        const n = Number(arr[i])
        if (!(n > 0) || seen[n])
            continue
        seen[n] = true
        nums.push(n)
    }
    nums.sort(function (a, b) { return a - b })
    return nums
}

/// Merge adjacent integers into inclusive {start, end} runs (1,2,3,7,9 -> 1-3, 7, 9).
function consecutiveRuns(arr) {
    const nums = uniqueSortedInts(arr)
    const runs = []
    for (let i = 0; i < nums.length; ) {
        const start = nums[i]
        let end = start
        let j = i + 1
        while (j < nums.length && nums[j] === end + 1) {
            end = nums[j]
            ++j
        }
        runs.push({ start: start, end: end })
        i = j
    }
    return runs
}

function passedThreshold(dx, dy, t) {
    return (dx * dx + dy * dy) >= (t * t)
}

/// Start a drag only after a deliberate move. Brief clicks and the two
/// presses of a double-click often jitter a few pixels; those must not arm
/// a service ghost. Before ``holdMs`` a larger distance is required so a
/// fast flick still counts as a drag.
function shouldStartDrag(dx, dy, threshold, pressAgeMs, holdMs) {
    const t = (threshold === undefined || threshold === null) ? 12 : Number(threshold)
    const hold = (holdMs === undefined || holdMs === null) ? 120 : Number(holdMs)
    const age = (pressAgeMs === undefined || pressAgeMs === null) ? hold : Number(pressAgeMs)
    if (age < hold)
        return passedThreshold(dx, dy, t * 2)
    return passedThreshold(dx, dy, t)
}

function modelValue(model, row, role) {
    if (!model || row < 0 || row >= model.rowCount())
        return null
    return model.data(model.index(row, 0), role)
}

function firstIndexFromY(view, x, y, yEnd, step) {
    const s = step || 4
    const dir = yEnd >= y ? 1 : -1
    const maxSteps = 500
    let yy = y
    for (let n = 0; n < maxSteps; ++n) {
        const idx = view.indexAt(x, yy)
        if (idx >= 0)
            return idx
        if ((dir > 0 && yy >= yEnd) || (dir < 0 && yy <= yEnd))
            break
        yy += dir * s
    }
    return -1
}

function listIndexRangeInContent(view, x1, y1, x2, y2) {
    if (!view || view.count <= 0)
        return []
    const top = Math.min(y1, y2)
    const bot = Math.max(y1, y2)
    const midX = (x1 + x2) / 2
    const x = Math.max(view.contentX + 4,
                       Math.min(view.contentX + Math.max(8, view.width - 8), midX))
    let start = firstIndexFromY(view, x, top, bot, 4)
    let end = firstIndexFromY(view, x, bot, top, 4)
    if (start < 0 && end < 0)
        return []
    if (start < 0)
        start = end
    if (end < 0)
        end = start
    const a = Math.min(start, end)
    const b = Math.max(start, end)
    const rows = []
    for (let i = a; i <= b; ++i)
        rows.push(i)
    return rows
}

function rectsIntersect(ax, ay, aw, ah, bx, by, bw, bh) {
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by
}

function isOverListRow(view, vx, vy) {
    if (!view)
        return false
    if (vx < 0 || vy < 0 || vx >= view.width || vy >= view.height)
        return false
    const item = view.itemAt(vx + view.contentX, vy + view.contentY)
    if (!item)
        return false
    const ix = vx + view.contentX
    const iy = vy + view.contentY
    return ix >= item.x && ix < item.x + item.width
            && iy >= item.y && iy < item.y + item.height
}

function autoScrollView(view, viewY, edge, step) {
    if (!view)
        return false
    const e = edge || 28
    const s = step || 12
    const maxY = Math.max(0, view.contentHeight - view.height)
    if (maxY <= 0)
        return false
    if (viewY < e) {
        const next = Math.max(0, view.contentY - s)
        if (next !== view.contentY) {
            view.contentY = next
            return true
        }
    } else if (viewY > view.height - e) {
        const next = Math.min(maxY, view.contentY + s)
        if (next !== view.contentY) {
            view.contentY = next
            return true
        }
    }
    return false
}
