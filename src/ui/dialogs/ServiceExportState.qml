import QtQuick
import SeventhSlide

// Service Settings' Export page as it stands: whose slides are exported, and
// how the last export went. An export runs at once, not on Save, under the
// themes the window holds (saved or not), and goes on in the background if
// the window closes; what it reports then is no longer shown.
QtObject {
    id: state

    required property SessionFacade session
    /// The service exported, and the per-output themes it is exported with.
    property var serviceId: -1
    property var themeMap: ({})
    /// The service's title: what the ZIP file is named for.
    property string serviceName: ""

    /// The output whose slides are exported; "" for every output.
    property string outputId: ""

    /// The export running for this window, or -1, and which it is: "images"
    /// or "package".
    property var requestId: -1
    property string runningKind: ""
    readonly property bool running: state.requestId >= 0
    /// How the last one went: "" (none yet), "done" or "failed", and what to
    /// say about it.
    property string outcome: ""
    property string outcomeText: ""
    /// Which export ``outcome`` is of: "images" or "package".
    property string outcomeKind: ""

    /// Where the save dialog starts: a file named for the service, in the
    /// folder the last export went to.
    function suggestedFile() {
        return state.session ? state.session.exporter.suggestedFile(state.serviceName) : ""
    }
    /// The same for the package, which is a ZIP file too.
    function suggestedPackageFile() {
        return state.session ? state.session.exporter.suggestedFileWith(state.serviceName, ".zip") : ""
    }

    /// Start again for another service (the window opening). An export still
    /// running for this service is followed on, so its progress and outcome
    /// show again, and a second cannot be started over it.
    function load(serviceId) {
        if (state.running && state.serviceId === serviceId)
            return
        state.serviceId = serviceId
        state.requestId = -1
        state.outcome = ""
        state.outcomeText = ""
    }

    /// Export the service's slides as images into ``file`` (a ZIP).
    function exportTo(file) {
        if (!state.session || state.serviceId < 0 || state.running)
            return
        state.outcome = ""
        state.outcomeText = ""
        state.runningKind = "images"
        state.requestId = state.session.exporter.exportServiceImages(state.serviceId, state.themeMap,
                                                                     state.outputId, file)
    }

    /// Export the whole service as a package into ``file`` (a ZIP): its
    /// rundown, its songs, Bible passages and Freeform items as a Freeform
    /// file, and its pictures, videos and audio.
    function exportPackageTo(file) {
        if (!state.session || state.serviceId < 0 || state.running)
            return
        state.outcome = ""
        state.outcomeText = ""
        state.runningKind = "package"
        state.requestId = state.session.exporter.exportServicePackage(state.serviceId, state.themeMap, file)
    }

    function describe(result) {
        if (!result.ok)
            return result.errors.join("\n")
        if (state.runningKind === "package") {
            const saved = qsTr("%n row(s) saved to %1.", "", result.count).arg(result.path)
            return result.notes && result.notes.length > 0 ? saved + "\n" + result.notes.join("\n") : saved
        }
        const saved = qsTr("%n image(s) saved to %1.", "", result.count).arg(result.path)
        if (result.skipped > 0)
            return saved + " " + qsTr("%n item(s) with no slides (video, audio, web pages, live "
                                      + "sources) left out.", "", result.skipped)
        return saved
    }

    readonly property Connections _finished: Connections {
        target: state.session ? state.session.exporter : null
        function onExportFinished(requestId, result) {
            if (requestId !== state.requestId)
                return
            state.requestId = -1
            state.outcome = result.ok ? "done" : "failed"
            state.outcomeKind = state.runningKind
            state.outcomeText = state.describe(result)
        }
    }
}
