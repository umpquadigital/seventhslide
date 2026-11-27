.pragma library

// Per-output theme selections ({outputId: {category: {theme, bg}}}) as the
// song editor and the service settings dialog both hold them, and the gallery
// thumb URL that shows one. Both screens edit the same shape for different
// owners (a song, a service), so the rules live here rather than in each
// dialog. Which categories and fields a map holds is the C++'s
// (ThemeController.pickSlots); what is stored is read in C++
// (theme::OutputThemeMap) on the way in and out.

/// The ``category``'s ``field`` ("theme" or "bg") id chosen for ``outputId``,
/// or "".
function themeId(map, outputId, category, field) {
    const entry = map ? map[outputId] : null
    const pick = entry ? entry[category] : null
    return pick ? String(pick[field] || "") : ""
}

/// A copy of ``map`` with ``outputId``'s ``category`` ``field`` set to
/// ``themeId``, or cleared when it is blank. A category left with no ids loses
/// its entry, and an output left with none loses its own.
function withThemeId(map, outputId, category, field, themeId) {
    const next = Object.assign({}, map || {})
    const entry = Object.assign({}, next[outputId] || {})
    const pick = Object.assign({}, entry[category] || {})
    const id = String(themeId || "").trim()
    if (id.length)
        pick[field] = id
    else
        delete pick[field]
    if (Object.keys(pick).length)
        entry[category] = pick
    else
        delete entry[category]
    if (Object.keys(entry).length)
        next[outputId] = entry
    else
        delete next[outputId]
    return next
}

/// Thumbnail URL for one theme on one output, or "" when there is nothing to
/// show. ``libraryRevision`` is session.themeLibraryRevision: it busts the
/// cached image when a theme is edited somewhere else.
/// The thumbnail shows the kind's own first context (the provider's choice).
function thumbUrl(outputName, kind, themeId, libraryRevision) {
    if (!outputName || !kind || !themeId)
        return ""
    const o = encodeURIComponent(outputName)
    const id = encodeURIComponent(themeId)
    return "image://themepreview/theme/" + o + "/" + encodeURIComponent(kind) + "/" + id
           + "?r=" + String(libraryRevision)
}
