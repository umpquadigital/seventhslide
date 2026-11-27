.pragma library

// How the Library panel's tabs are arranged, from the ids kept on this computer
// (LibraryController). What is kept may be older or newer than this version, so
// it is only ever taken as a wish: ids no longer known are dropped, and a tab
// it does not mention yet takes its default place at the end.

/// Every tab's id, in the default order.
const ids = ["songs", "freeform", "bibles", "videos", "images", "audio", "browser", "live"]

/// Every tab's id, in order ``order``.
function arranged(order) {
    const out = []
    for (let i = 0; i < order.length; ++i) {
        if (ids.indexOf(order[i]) >= 0 && out.indexOf(order[i]) < 0)
            out.push(order[i])
    }
    for (let i = 0; i < ids.length; ++i) {
        if (out.indexOf(ids[i]) < 0)
            out.push(ids[i])
    }
    return out
}

/// The ids in ``stored`` that hide a tab. Not all of them: the strip keeps one.
function hidden(stored) {
    const out = ids.filter(id => stored.indexOf(id) >= 0)
    return out.length === ids.length ? [] : out
}

/// The tabs the strip shows, in order: never none.
function shown(order, stored) {
    const off = hidden(stored)
    return arranged(order).filter(id => off.indexOf(id) < 0)
}
