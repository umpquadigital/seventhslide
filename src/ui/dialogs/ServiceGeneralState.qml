import QtQuick
import SeventhSlide

// Service Settings' General page as it stands: the service's title, date,
// time, tags and audio fades as typed, against what they were when the window
// opened. Nothing is kept until save(), which keeps only what changed.
QtObject {
    id: state

    /// The service as it was opened ({name, date, time, timeLabel, tags}:
    /// LibraryController.serviceDetails).
    property var saved: ({})
    property string draftName: ""
    /// ``yyyy-MM-dd``; "" for no date.
    property string draftDate: ""
    /// The time as typed: "7:30 PM", "1930"...
    property string draftTimeText: ""
    property var draftTags: []
    /// The audio fades the service sets, as typed: {fade_in_ms, fade_out_ms,
    /// overlap_ms}, each "" to follow each output's own, and on_play_pause
    /// "on", "off" or "" (the outputs').
    property var draftFades: ({ fade_in_ms: "", fade_out_ms: "", overlap_ms: "",
                                on_play_pause: "" })

    /// Start again from ``details`` (serviceDetails).
    function load(details) {
        state.saved = details
        state.draftName = details.name
        state.draftDate = details.date
        state.draftTimeText = details.timeLabel
        state.draftTags = details.tags
        state.draftFades = state.fadesAsTyped(details.audioFades)
    }

    /// ``set`` ({key: ms} for the fields set) as the fields show it: "" for
    /// each not set.
    function fadesAsTyped(set) {
        const shown = function (key) {
            return set && set[key] !== undefined ? String(set[key]) : ""
        }
        const onPlayPause = set && set.on_play_pause !== undefined
                            ? (set.on_play_pause ? "on" : "off") : ""
        return { fade_in_ms: shown("fade_in_ms"), fade_out_ms: shown("fade_out_ms"),
                 overlap_ms: shown("overlap_ms"), on_play_pause: onPlayPause }
    }

    /// The fades typed, as numbers: only those set, as setServiceAudioFades
    /// takes them.
    function fadesSet() {
        const out = {}
        for (const key of ["fade_in_ms", "fade_out_ms", "overlap_ms"]) {
            const n = parseInt(state.draftFades[key], 10)
            if (!isNaN(n))
                out[key] = n
        }
        if (state.draftFades.on_play_pause.length)
            out.on_play_pause = state.draftFades.on_play_pause === "on"
        return out
    }

    /// Whether fades ``a`` and ``b`` ({key: value}) set the same, in any order.
    function sameFades(a, b) {
        const canon = function (o) {
            o = o || {}
            return JSON.stringify(o, Object.keys(o).sort())
        }
        return canon(a) === canon(b)
    }

    function setFade(key, text) {
        const next = Object.assign({}, state.draftFades)
        next[key] = text
        state.draftFades = next
    }

    /// Whether anything differs from what was opened.
    function hasChanges() {
        return state.draftName.trim() !== state.saved.name
            || state.draftDate !== state.saved.date
            || state.draftTimeText !== state.saved.timeLabel
            || JSON.stringify(state.draftTags) !== JSON.stringify(state.saved.tags)
            || !state.sameFades(state.fadesSet(), state.saved.audioFades)
    }

    /// ``text`` as ``HH:mm``: "7:30", "19:30", "7:30 pm", "730pm", "7pm",
    /// "19:30:00" (seconds dropped). "" for nothing typed; null for what is
    /// not a time.
    function parseTime(text) {
        const t = String(text || "").trim().toLowerCase().replace(/\s+|\./g, "")
        if (!t.length)
            return ""
        const m = /^(\d{1,2})(?::?(\d{2})(?::\d{2})?)?(am|pm|a|p)?$/.exec(t)
        if (!m)
            return null
        let h = Number(m[1])
        const min = m[2] ? Number(m[2]) : 0
        if (m[3]) {
            if (h < 1 || h > 12)
                return null
            if (m[3][0] === "p" && h < 12)
                h += 12
            if (m[3][0] === "a" && h === 12)
                h = 0
        }
        if (h > 23 || min > 59)
            return null
        const pad = function (n) { return (n < 10 ? "0" : "") + n }
        return pad(h) + ":" + pad(min)
    }

    /// When the service is held, as setServiceDateTime takes it: "" for no
    /// date, midnight for a date with no time.
    function scheduleIso(date, time) {
        return date.length ? date + "T" + (time.length ? time : "00:00") + ":00" : ""
    }

    /// Add tag ``text``, tidied, unless the service has it in any case.
    function addTag(text) {
        const tag = String(text || "").trim().replace(/\s+/g, " ")
        if (!tag.length)
            return
        const lower = tag.toLowerCase()
        if (state.draftTags.some(function (t) { return String(t).toLowerCase() === lower }))
            return
        state.draftTags = state.draftTags.concat([tag])
    }

    function removeTag(index) {
        if (index < 0 || index >= state.draftTags.length)
            return
        const next = state.draftTags.slice()
        next.splice(index, 1)
        state.draftTags = next
    }

    /// Keep what changed on service ``id`` through ``library``. "" when kept;
    /// otherwise why not (a time that is not one is refused before anything
    /// is kept).
    function save(library, id) {
        // The time as it opened is the locale's label of the stored one, which
        // the parser need not read ("오전 11:00"): left as it was, it is that.
        const time = state.draftTimeText === state.saved.timeLabel ? (state.saved.time || "")
                                                                   : state.parseTime(state.draftTimeText)
        if (state.draftDate.length && time === null)
            return qsTr("“%1” is not a time. Try 9:30 or 7:00 PM.").arg(state.draftTimeText.trim())
        let err = ""
        if (state.draftName.trim() !== state.saved.name)
            err = library.renameService(id, state.draftName)
        const iso = state.scheduleIso(state.draftDate, time || "")
        if (!err.length && iso !== state.scheduleIso(state.saved.date, state.saved.time))
            err = library.setServiceDateTime(id, iso)
        if (!err.length && JSON.stringify(state.draftTags) !== JSON.stringify(state.saved.tags))
            err = library.setServiceTags(id, state.draftTags)
        if (!err.length
                && !state.sameFades(state.fadesSet(), state.saved.audioFades))
            err = library.setServiceAudioFades(id, state.fadesSet())
        return err
    }
}
