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
    /// The date's day, month and year as typed in their three fields:
    /// "03", "4", "2026", "26"...; all "" for no date.
    property string draftDay: ""
    property string draftMonth: ""
    property string draftYear: ""
    /// The date typed, as ``yyyy-MM-dd``: "" for none, null for what is not
    /// a date (a field left empty, February 30).
    readonly property var draftDate: state.dateOf(state.draftDay, state.draftMonth, state.draftYear)
    /// Whether any of the date's fields has something in it.
    readonly property bool dateTyped: (state.draftDay + state.draftMonth + state.draftYear).trim().length > 0
    /// The order of the date's fields, "MDY", "DMY" or "YMD", and what shows
    /// between them: the app's locale's short date (en_US: MM / DD / YYYY).
    readonly property string dateOrder: state.orderOf(Qt.locale().dateFormat(Locale.ShortFormat))
    readonly property string dateSeparator: state.separatorOf(Qt.locale().dateFormat(Locale.ShortFormat))
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
        const day = /^(\d{4})-(\d{2})-(\d{2})$/.exec(String(details.date || ""))
        state.draftYear = day ? day[1] : ""
        state.draftMonth = day ? day[2] : ""
        state.draftDay = day ? day[3] : ""
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

    /// The order of the day, month and year in date format ``format``
    /// ("M/d/yy" is "MDY"); "MDY" when it has not all three.
    function orderOf(format) {
        const f = String(format || "")
        const at = { M: f.indexOf("M"), D: f.indexOf("d"), Y: f.indexOf("y") }
        if (at.M < 0 || at.D < 0 || at.Y < 0)
            return "MDY"
        return ["M", "D", "Y"].sort(function (a, b) { return at[a] - at[b] }).join("")
    }

    /// What goes between the parts of date format ``format``: "/" in
    /// "M/d/yy", "." in "dd.MM.yy" and "d. M. yyyy"; "/" when it is a space
    /// or a word. Spaces and the marks that set a script's direction (Arabic's
    /// "d\u200f/M\u200f/y") around it are not it.
    function separatorOf(format) {
        const m = /[dMy]+[\s\u200e\u200f\u061c]*([^dMy\s'\u200e\u200f\u061c])/.exec(String(format || ""))
        return m ? m[1] : "/"
    }

    /// Date field ``part`` ("D", "M" or "Y") as typed.
    function datePart(part) {
        return part === "D" ? state.draftDay : part === "M" ? state.draftMonth : state.draftYear
    }

    function setDatePart(part, text) {
        if (part === "D")
            state.draftDay = text
        else if (part === "M")
            state.draftMonth = text
        else
            state.draftYear = text
    }

    /// Date field ``part`` ("D", "M" or "Y") as it reads once typed: a day or
    /// month as two digits ("4" is "04"), a two-digit year as this century's
    /// ("26" is "2026"). Anything else as it is.
    function tidyDatePart(part, text) {
        const t = String(text || "").trim()
        if (part === "Y")
            return /^\d{2}$/.test(t) ? "20" + t : t
        return /^[1-9]$/.test(t) ? "0" + t : t
    }

    /// The date ``day``, ``month``, ``year`` make, as ``yyyy-MM-dd``. A
    /// two-digit year is this century's. "" when all three are empty; null
    /// when one is, when the year is not two or four digits (or is before
    /// 1000), or when they make no day (February 30, month 13).
    function dateOf(day, month, year) {
        const d = String(day || "").trim()
        const m = String(month || "").trim()
        const y = state.tidyDatePart("Y", year)
        if (!d.length && !m.length && !y.length)
            return ""
        if (!/^\d{1,2}$/.test(d) || !/^\d{1,2}$/.test(m) || !/^\d{4}$/.test(y))
            return null
        const yn = Number(y)
        const mn = Number(m)
        const dn = Number(d)
        const at = new Date(yn, mn - 1, dn)
        if (yn < 1000 || at.getFullYear() !== yn || at.getMonth() !== mn - 1 || at.getDate() !== dn)
            return null
        const pad = function (n) { return (n < 10 ? "0" : "") + n }
        return yn + "-" + pad(mn) + "-" + pad(dn)
    }

    /// ``text`` as ``HH:mm``: "7:30", "19:30", "7:30 pm", "730pm", "7pm". ""
    /// for nothing typed; null for what is not a time.
    function parseTime(text) {
        const t = String(text || "").trim().toLowerCase().replace(/\s+|\./g, "")
        if (!t.length)
            return ""
        const m = /^(\d{1,2})(?::?(\d{2}))?(am|pm|a|p)?$/.exec(t)
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
    /// otherwise why not (a date or time that is not one is refused before
    /// anything is kept).
    function save(library, id) {
        if (state.draftDate === null) {
            const parts = [state.draftDay, state.draftMonth, state.draftYear]
            if (parts.some(function (p) { return !String(p).trim().length }))
                return qsTr("Type the service’s day, month and year, or leave all three empty.")
            const typed = state.dateOrder.split("").map(function (c) {
                return state.tidyDatePart(c, state.datePart(c))
            })
            return qsTr("“%1” is not a date.").arg(typed.join(state.dateSeparator))
        }
        const time = state.parseTime(state.draftTimeText)
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
