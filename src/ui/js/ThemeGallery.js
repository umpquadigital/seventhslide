.pragma library

// The theme gallery's browsing rules - search, the defaults-only switch, tag
// filter, sort and the tag menu - shared by the output dialog's gallery and
// the theme picker. Pure functions over the theme maps
// SessionFacade.themes.list returns; each dialog keeps its own state.

/// ``themes`` narrowed and ordered for display.
///
/// ``opts``: ``query`` (name or tag substring), ``tagFilter`` (one tag or ""),
/// ``defaultsOnly`` with ``defaultIds`` (the song / Bible defaults),
/// ``sortKey`` ("name" | "added"), ``sortAsc``, and ``tagsOf(theme)``.
function filterAndSort(themes, opts) {
    const q = String(opts.query || "").trim().toLowerCase()
    const defaults = opts.defaultIds || []
    const list = (themes || []).filter(function (t) {
        if (opts.defaultsOnly && defaults.indexOf(t.id) < 0)
            return false
        const tags = opts.tagsOf(t)
        if (opts.tagFilter.length && tags.indexOf(opts.tagFilter) < 0)
            return false
        if (!q.length)
            return true
        if (String(t.name || "").toLowerCase().indexOf(q) >= 0)
            return true
        for (let i = 0; i < tags.length; ++i) {
            if (tags[i].indexOf(q) >= 0)
                return true
        }
        return false
    })
    const dir = opts.sortAsc ? 1 : -1
    list.sort(function (a, b) {
        let cmp = 0
        if (opts.sortKey === "added")
            cmp = compare(String(a.createdAt || ""), String(b.createdAt || ""))
        if (cmp === 0)
            cmp = compare(String(a.name || "").toLowerCase(), String(b.name || "").toLowerCase())
        if (cmp === 0)
            cmp = compare(String(a.id || ""), String(b.id || ""))
        return dir * cmp
    })
    return list
}

function compare(a, b) {
    return a < b ? -1 : (a > b ? 1 : 0)
}

/// Every tag the themes carry, once, sorted.
function tagCatalog(themes, tagsOf) {
    const seen = ({})
    const out = []
    for (let i = 0; i < (themes || []).length; ++i) {
        const tags = tagsOf(themes[i])
        for (let j = 0; j < tags.length; ++j) {
            const t = tags[j]
            if (!t.length || seen[t])
                continue
            seen[t] = true
            out.push(t)
        }
    }
    out.sort()
    return out
}

/// The tag menu's rows for ``query``: "all" (``allLabel``) then each match.
function tagMenuItems(tags, query, allLabel) {
    const q = String(query || "").trim().toLowerCase()
    const out = []
    if (!q.length || allLabel.toLowerCase().indexOf(q) >= 0)
        out.push({ tag: "", label: allLabel })
    for (let i = 0; i < tags.length; ++i) {
        if (q.length && tags[i].toLowerCase().indexOf(q) < 0)
            continue
        out.push({ tag: tags[i], label: tags[i] })
    }
    return out
}
