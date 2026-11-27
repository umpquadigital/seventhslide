import QtQuick
import SeventhSlide

// The Output editor's Destinations tab: where this output is sent (screens,
// desktop windows, NDI, SRT streams, Blackmagic, virtual cameras), adding and removing them, and the
// targets a destination's menu picks from. Every change persists at once;
// errors go out through ``reported``.
QtObject {
    id: state

    property SessionFacade session
    /// The output whose destinations these are.
    property string outputId: ""
    /// Its display name: what "used by" labels say for it.
    property string outputName: ""
    /// The tab is on screen: the list follows the session while it is.
    property bool shown: false
    /// The output's canvas in pixels (as General has it, saved or not): the
    /// size a destination matches, or scales down from.
    property int canvasW: 1920
    property int canvasH: 1080

    /// A result to show: an error, or "" to clear the last one.
    signal reported(string error)

    property var destinations: []
    /// The destination whose settings show, or "" when there is none.
    property string selectedDestId: ""
    property var ndiRuntime: ({ available: false, version: "", path: "", message: "" })
    property var bmdRuntime: ({ available: false, version: "", path: "", message: "", deviceCount: 0 })
    property var bmdDevices: []
    property var bmdModes: []
    property var cameras: []
    /// What the selected display's or window's sound can play on ({key,
    /// label, suggested}), none first. Asked for when one is selected or its
    /// menu opens: listing the devices asks the sound system.
    property var soundDevices: []
    onSelectedDestIdChanged: state.loadStreamCodecs()
    /// The video codecs an SRT stream can use here ({key, available, encoder,
    /// hardware}). Asked for once an SRT destination shows: the first ask
    /// tries this computer's encoders.
    property var streamCodecs: []

    function loadDestinations() {
        if (!state.session || !state.outputId) {
            state.destinations = []
            state.selectedDestId = ""
            return
        }
        state.session.destinations.refreshDisplays()
        state.cameras = state.session.destinations.virtualCameras() || []
        state.bmdDevices = state.session.destinations.blackmagicDevices() || []
        state.ndiRuntime = state.session.destinations.ndiRuntimeInfo() || state.ndiRuntime
        const list = state.session.destinations.list(state.outputId) || []
        state.destinations = list
        // The selection stays while it exists; otherwise the first shows, so
        // the settings beside the list are never empty while there is one.
        if (!list.some(function (d) { return d.id === state.selectedDestId }))
            state.selectedDestId = list.length ? list[0].id : ""
        // A display's sound devices, once: listing them asks the sound
        // system, and this runs after every change.
        const shown = state.selectedDestination()
        if (state.destHasSoundDevice(shown) && state.soundDevices.length === 0)
            state.loadSoundDevices()
        state.loadStreamCodecs()
    }

    /// The list again, for what running destinations report (receivers, a
    /// stream's connection): nothing else is asked for.
    function reloadStatus() {
        if (state.session && state.outputId)
            state.destinations = state.session.destinations.list(state.outputId) || []
    }

    /// The codecs an SRT stream can use, once one shows. Empty while this
    /// computer's encoders are being tried; asked again when they are known.
    function loadStreamCodecs() {
        const shown = state.selectedDestination()
        if (state.session && shown && shown.type === "srt" && state.streamCodecs.length === 0)
            state.streamCodecs = state.session.destinations.streamCodecs() || []
    }

    /// Show destination ``id``'s settings.
    function selectDestination(id) {
        if (state.selectedDestId === id)
            return
        state.selectedDestId = id
        state.reported("")
        state.loadSoundDevices()
    }

    /// The selected destination's sound devices, when it is a display or a
    /// window.
    function loadSoundDevices() {
        const dest = state.selectedDestination()
        state.soundDevices = state.session && state.destHasSoundDevice(dest)
                             ? (state.session.destinations.soundDevices(dest.id) || []) : []
    }

    /// ``dest`` has a window whose sound plays on a device of its own (a
    /// display or a window), rather than sent with its picture.
    function destHasSoundDevice(dest) {
        return !!dest && (dest.type === "display" || dest.type === "window")
    }

    /// The sound device a display or window plays on, as its field shows it.
    function destSoundLabel(dest) {
        if (!dest || !dest.audioDevice)
            return qsTr("None")
        for (let i = 0; i < state.soundDevices.length; ++i) {
            if (state.soundDevices[i].key === dest.audioDevice)
                return state.soundDevices[i].label
        }
        return qsTr("Not connected")
    }

    function setDestinationAudioDevice(id, key) {
        if (!state.session || !id)
            return false
        return state.applyDestPick(state.session.destinations.setAudioDevice(id, key))
    }

    function selectedDestination() {
        return state.destinationById(state.selectedDestId)
    }

    /// The destination ``id`` as last loaded, or null.
    function destinationById(id) {
        const list = state.destinations
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === id)
                return list[i]
        }
        return null
    }

    function destTitle(dest) {
        if (!dest)
            return ""
        const n = dest.name ? String(dest.name).trim() : ""
        // Skip Wayland-style hex ids ("0x40a0"); C++ also rewrites these.
        if (n.length && !/^0x[0-9a-f]+$/i.test(n))
            return n
        return state.destScreenLabel(dest)
    }

    /// The destAddTypes() row for ``type``, or null for a type it does not list.
    function destTypeRow(type) {
        const types = state.destAddTypes()
        for (let i = 0; i < types.length; ++i) {
            if (types[i].type === type)
                return types[i]
        }
        return null
    }

    /// An unknown type is named and drawn as a display (the first row).
    function destTypeLabel(type) {
        return (state.destTypeRow(type) || state.destAddTypes()[0]).label
    }

    function destTypeIcon(type) {
        return (state.destTypeRow(type) || state.destAddTypes()[0]).icon
    }

    function destTypeAvailable(type) {
        if (!state.session)
            return type === "display"
        const raw = state.session.destinations.types() || []
        for (let i = 0; i < raw.length; ++i) {
            if (raw[i].type === type)
                return !!raw[i].available
        }
        return type === "display"
    }

    /// The C++ row for ``type`` from destinations.types(), or null.
    function destTypeInfo(type) {
        const raw = state.session ? (state.session.destinations.types() || []) : []
        for (let i = 0; i < raw.length; ++i) {
            if (raw[i].type === type)
                return raw[i]
        }
        return null
    }

    /// The types the "add destination" menu offers: those this system can
    /// have at all (no virtual camera off Linux).
    function destAddChoices() {
        return state.destAddTypes().filter(t => {
            const info = state.destTypeInfo(t.type)
            return !info || info.built !== false
        })
    }

    function destAddTypes() {
        return [
            { type: "display", label: qsTr("Display"), hint: qsTr("A connected monitor"), icon: "display" },
            { type: "window", label: qsTr("Window"), hint: qsTr("A window to move and resize on the desktop"), icon: "window" },
            { type: "ndi", label: qsTr("NDI"), hint: qsTr("Send over the network"), icon: "ndi" },
            { type: "srt", label: qsTr("SRT"), hint: qsTr("Stream over a network or the internet"), icon: "cast" },
            { type: "bmd", label: qsTr("Blackmagic"), hint: qsTr("DeckLink / Intensity card"), icon: "bmd" },
            { type: "vcam", label: qsTr("Virtual Camera"), hint: qsTr("Zoom, Teams, and other apps"), icon: "video" }
        ]
    }

    function addDestination(type) {
        if (!state.session || !state.destTypeAvailable(type))
            return
        const result = state.session.destinations.add(state.outputId, type)
        if (result && result.error) {
            state.reported(result.error)
            return
        }
        state.reported("")
        if (result && result.id)
            state.selectedDestId = result.id
        state.loadDestinations()
    }

    // --- Changing a destination ---
    // Each setter sends one change and shows its result (an empty result
    // clears the last error); an accepted change reloads the list so the
    // inspector matches the host. tests/test_ui_destinations.cpp pins each.
    //   - A target picked from a menu (screen, camera, Blackmagic device or
    //     mode, sound device) says whether it was taken, and the menu closes
    //     once it was. A refusal leaves it open, with the reason shown, to
    //     pick another; the host has not changed, so nothing reloads.
    //   - A rename or a remove that is refused stops there too, so the field
    //     keeps what was typed.
    //   - Enabled, NDI name, alpha, audio and format reload either way.
    // An accepted remove also drops the selection.

    /// The result of a target picked from a menu (see above): whether it was
    /// taken.
    function applyDestPick(err) {
        state.reported(err)
        if (err.length)
            return false
        state.loadDestinations()
        return true
    }

    function removeDestination(id) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.remove(id)
        state.reported(err)
        if (err.length)
            return
        if (state.selectedDestId === id)
            state.selectedDestId = ""
        state.loadDestinations()
    }

    /// Destination ``id`` has a neighbour to pass ``step`` (-1 up, 1 down).
    function canMoveDestination(id, step) {
        const at = state.destinations.findIndex(function (d) { return d.id === id })
        return at >= 0 && at + step >= 0 && at + step < state.destinations.length
    }

    /// The position of destination ``id`` in the list, or -1.
    function destinationIndex(id) {
        return state.destinations.findIndex(function (d) { return d.id === id })
    }

    function moveDestination(id, step) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.move(id, step)
        state.reported(err)
        if (err.length)
            return
        state.loadDestinations()
    }

    function setDestinationName(id, name) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.setName(id, name)
        state.reported(err)
        if (err.length)
            return
        state.loadDestinations()
    }

    function setDestinationEnabled(id, enabled) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.setEnabled(id, enabled)
        state.reported(err)
        state.loadDestinations()
    }

    function setDestinationIgnoreLive(id, ignore) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.setIgnoreLive(id, ignore)
        state.reported(err)
        state.loadDestinations()
    }

    function setDestinationNdiName(id, name) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.setNdiName(id, name)
        state.reported(err)
        state.loadDestinations()
    }

    function setDestinationAlpha(id, on) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.setAlpha(id, on)
        state.reported(err)
        state.loadDestinations()
    }

    /// This computer's NDI transport, a session.ndiTransport key: every NDI
    /// source's.
    function setNdiTransport(transport) {
        if (state.session)
            state.reported(state.session.setNdiTransport(transport))
    }

    function setDestinationAudio(id, on) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.setAudio(id, on)
        state.reported(err)
        state.loadDestinations()
    }

    function setDestinationScreen(id, key) {
        if (!state.session || !id)
            return false
        return state.applyDestPick(state.session.destinations.setScreen(id, key))
    }

    function setDestinationBmdDevice(id, key) {
        if (!state.session || !id)
            return false
        return state.applyDestPick(state.session.destinations.setBmdDevice(id, key))
    }

    function setDestinationBmdMode(id, key) {
        if (!state.session || !id)
            return false
        return state.applyDestPick(state.session.destinations.setBmdMode(id, key))
    }

    function setDestinationCamera(id, key) {
        if (!state.session || !id)
            return false
        return state.applyDestPick(state.session.destinations.setCamera(id, key))
    }

    // --- A destination's format ---
    // A destination is drawn as the output is, or (not matching it) no
    // taller than a height picked, in the same shape: never larger, as there
    // is nothing more to send. An NDI source can instead carry the sound
    // alone. Not Blackmagic: its video mode decides its format. The host
    // keeps each destination's (maxHeight, video) and applies it live.

    /// The destination can be given a size of its own.
    function destSizable(dest) {
        return !!dest && !!dest.sizable
    }

    /// The sizes a destination can be drawn at, largest first ({w, h, label}):
    /// the output's, then the usual heights under it in its shape.
    function destSizeOptions() {
        const rows = state.session
                ? (state.session.destinations.sizeOptions(state.canvasW, state.canvasH) || [])
                : []
        return rows.map(function (r) {
            return { w: r.w, h: r.h, label: qsTr("%1p").arg(r.h) }
        })
    }

    function destMatchesOutput(dest) {
        return !dest || !(dest.maxHeight > 0) && dest.video !== false
    }

    /// The size ``dest`` is drawn at: the output's, or the tallest option no
    /// taller than its own (the canvas may have shrunk since it was picked).
    function destSize(dest) {
        const options = state.destSizeOptions()
        if (!options.length)
            return null
        if (!dest || !(dest.maxHeight > 0))
            return options[0]
        for (let i = 0; i < options.length; ++i) {
            if (options[i].h <= dest.maxHeight)
                return options[i]
        }
        return options[options.length - 1]
    }

    function destSizeText(dest) {
        if (state.destAudioOnly(dest))
            return qsTr("No video")
        const size = state.destSize(dest)
        return size ? qsTr("%1 × %2").arg(size.w).arg(size.h) : ""
    }

    /// Send ``dest``'s format to the host and show the result.
    function setDestinationFormat(id, maxHeight, video) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.setFormat(id, maxHeight, video)
        state.reported(err)
        state.loadDestinations()
    }

    /// Draw ``id`` as its output is (``on``), or let it have a size of its
    /// own, starting from the output's.
    function setDestinationMatchesOutput(id, on) {
        state.setDestinationFormat(id, on ? 0 : state.canvasH, true)
    }

    /// ``dest`` can be sent as sound alone: an NDI source not matching the
    /// output.
    function destCanBeAudioOnly(dest) {
        return !!dest && !!dest.soundAloneCapable && !state.destMatchesOutput(dest)
    }

    function destAudioOnly(dest) {
        return !!dest && dest.video === false
    }

    /// Send ``id`` as sound alone (``on``), or with its picture again.
    function setDestinationAudioOnly(id, on) {
        const dest = state.destinationById(id)
        if (dest)
            state.setDestinationFormat(id, dest.maxHeight || state.canvasH, !on)
    }

    /// Draw ``id`` no taller than ``height``, in the output's shape.
    function setDestinationSize(id, height) {
        const dest = state.destinationById(id)
        if (dest)
            state.setDestinationFormat(id, height, dest.video !== false)
    }

    // --- An SRT stream ---

    /// Change how SRT destination ``id`` streams: ``changes`` by its row's
    /// ``stream`` names, and "passphrase".
    function setDestinationStream(id, changes) {
        if (!state.session || !id)
            return
        const err = state.session.destinations.setStream(id, changes)
        state.reported(err)
        state.loadDestinations()
    }

    /// The Video dropdown: each codec this computer can encode, with the
    /// kind of encoder it would use.
    function streamCodecOptions() {
        const names = { h265: qsTr("H.265"), h264: qsTr("H.264") }
        const out = []
        for (let i = 0; i < state.streamCodecs.length; ++i) {
            const c = state.streamCodecs[i]
            if (c.available)
                out.push({ value: c.key, label: names[c.key] || c.key,
                           note: c.hardware ? qsTr("Hardware") : qsTr("Software") })
        }
        return out
    }

    /// The Bitrate dropdown: Automatic (what it picks for the size sent),
    /// then fixed rates.
    function streamBitrateOptions(dest) {
        const size = state.destSize(dest)
        const rates = state.session && dest && size
                ? (state.session.destinations.streamRates(dest.id, size.w, size.h) || {}) : {}
        const mbps = function (kbps) { return (kbps / 1000).toLocaleString(Qt.locale(), "f", 1) }
        const out = [{ value: 0, label: qsTr("Automatic"),
                       note: rates.auto ? qsTr("%1 Mb/s").arg(mbps(rates.auto)) : "" }]
        const fixed = [3000, 5000, 8000, 12000, 20000, 30000]
        for (let i = 0; i < fixed.length; ++i)
            out.push({ value: fixed[i], label: qsTr("%1 Mb/s").arg(mbps(fixed[i])) })
        return out
    }

    /// What encoder ``row`` (a destination's) streams with, for its note.
    function streamEncoderText(dest) {
        const name = (dest && dest.encoder) || ""
        const apis = { vaapi: "VAAPI", nvenc: "NVENC", qsv: "Quick Sync",
                       videotoolbox: "VideoToolbox" }
        if (name.startsWith("lib"))
            return qsTr("software (%1)").arg(name.substring(3))
        const api = name.substring(name.lastIndexOf("_") + 1)
        return qsTr("hardware (%1)").arg(apis[api] || api)
    }

    /// The line under an SRT destination saying where its stream is, or ""
    /// while it is not running.
    function streamStatusText(dest) {
        if (!dest || dest.type !== "srt" || !dest.enabled)
            return ""
        const s = dest.stream || {}
        const codec = s.codec === "h264" ? qsTr("H.264") : qsTr("H.265")
        switch (dest.streamState) {
        case "waiting": {
            const at = s.reachAt || []
            return at.length
                    ? qsTr("Waiting for a receiver. Open %1 in OBS, vMix or a decoder.")
                          .arg(at.join(qsTr(" or ")))
                    : qsTr("Waiting for a receiver on port %1.").arg(s.port)
        }
        case "connecting":
            return qsTr("Connecting to %1…").arg(s.address)
        case "sending":
            return qsTr("Sending %1 at %2 Mb/s, %3 encoder.")
                    .arg(codec)
                    .arg((dest.bitrateKbps / 1000).toLocaleString(Qt.locale(), "f", 1))
                    .arg(state.streamEncoderText(dest))
        case "retrying":
            return qsTr("%1 Trying again.").arg(dest.streamError)
        case "failed":
            return dest.streamError
        }
        return ""
    }

    /// The stream is going: its status line reads as good news.
    function streamStatusOk(dest) {
        return !!dest && dest.streamState === "sending"
    }

    /// A destination menu entry: its name, and the output already using it
    /// when that is not this one.
    function destMenuLabel(name, assignedOutput) {
        if (assignedOutput && assignedOutput.length && assignedOutput !== state.outputName)
            return qsTr("%1 (used by %2)").arg(name).arg(assignedOutput)
        return name
    }

    function destReceiversText(dest) {
        if (!dest || dest.type !== "ndi" || !dest.live)
            return ""
        const n = dest.receivers
        if (n === undefined || n === null || n < 0)
            return ""
        if (n === 0)
            return qsTr("On the network, no receivers connected yet")
        return n === 1 ? qsTr("1 receiver connected")
                       : qsTr("%1 receivers connected").arg(n)
    }

    function destBmdDeviceLabel(dest) {
        if (!dest)
            return ""
        for (let i = 0; i < state.bmdDevices.length; ++i) {
            if (state.bmdDevices[i].key === dest.targetKey)
                return state.bmdDevices[i].label
        }
        if (dest.targetKey && dest.targetKey.length)
            return qsTr("Not connected")
        return qsTr("Choose a device")
    }

    function destBmdModeLabel(dest) {
        if (!dest)
            return ""
        for (let i = 0; i < state.bmdModes.length; ++i) {
            if (state.bmdModes[i].key === dest.modeKey)
                return state.bmdModes[i].name
        }
        if (dest.modeName && dest.modeName.length)
            return dest.modeName
        // Empty mode is legitimate: the card picks until the operator does.
        return qsTr("Automatic")
    }

    /// The selected Blackmagic device, if it reports a keyer at all.
    function destBmdCanKey(dest) {
        if (!dest || dest.type !== "bmd")
            return false
        for (let i = 0; i < state.bmdDevices.length; ++i) {
            if (state.bmdDevices[i].key === dest.targetKey)
                return !!state.bmdDevices[i].supportsInternalKeying
                       || !!state.bmdDevices[i].supportsExternalKeying
        }
        return false
    }

    function destScreenLabel(dest) {
        if (dest && dest.type === "vcam")
            return state.destCameraLabel(dest)
        if (dest && dest.screenLabel && dest.screenLabel.length)
            return dest.screenLabel
        if (dest && dest.screenKey && dest.screenKey.length)
            return dest.screenKey
        return qsTr("Choose a display")
    }

    function destCameraLabel(dest) {
        if (dest && dest.screenLabel && dest.screenLabel.length)
            return dest.screenLabel
        if (dest && dest.screenKey && dest.screenKey.length)
            return dest.screenKey
        return qsTr("No device found")
    }

    function destConflictText(dest) {
        if (!dest || !dest.conflictOutput)
            return ""
        if (dest.conflictOutput === state.outputName)
            return qsTr("Used by another destination")
        return qsTr("Used by %1").arg(dest.conflictOutput)
    }

    /// The targets a ``kind`` menu lists (see OutputDestinationMenu), asked
    /// for again as it opens.
    function loadTargets(kind) {
        if (state.session)
            state.session.destinations.refreshDisplays()
        state.cameras = state.session ? (state.session.destinations.virtualCameras() || []) : []
        if (kind === "sound")
            state.loadSoundDevices()
        if (state.session && (kind === "bmd" || kind === "bmdmode")) {
            state.bmdDevices = state.session.destinations.blackmagicDevices() || []
            const d = state.selectedDestination()
            state.bmdModes = (d && d.targetKey)
                    ? (state.session.destinations.blackmagicModes(d.targetKey) || []) : []
        }
    }

    /// Probe the NDI and Blackmagic runtimes again: the operator may have
    /// installed one since the app started. Cheap when one is already loaded.
    function refreshRuntimes() {
        if (!state.session)
            return
        state.ndiRuntime = state.session.destinations.refreshNdiRuntime() || state.ndiRuntime
        state.bmdRuntime = state.session.destinations.refreshBmdRuntime() || state.bmdRuntime
    }

    readonly property Connections destinationsWatch: Connections {
        target: state.session ? state.session.destinations : null
        function onChanged() {
            if (state.shown)
                state.loadDestinations()
        }
        function onStatusChanged() {
            if (state.shown)
                state.reloadStatus()
        }
        function onStreamCodecsReady() {
            if (state.shown)
                state.loadStreamCodecs()
        }
    }
}
