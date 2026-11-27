pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Download from the internet: paste a link, look it up, and pick what to
// get - the best picture, a smaller one, or the sound alone. Video goes to
// Videos and sound to Audio, each into the folder that tab is showing.
// yt-dlp does the work (session.download); the downloads show in the bottom
// bar. Without yt-dlp, or when a site has changed under it, it offers to
// install or update the app's own copy and look again.
AppDialog {
    id: root

    required property SessionFacade session
    /// Where the files go: the folder each tab shows (-1: none).
    property var videoFolderId: -1
    property var audioFolderId: -1

    property string url: ""
    property var lookupId: -1
    property bool looking: false
    /// The lookup's result while it is the link's (see DownloadController).
    property var info: null
    property string error: ""
    /// No yt-dlp at all; a newer yt-dlp would likely do.
    property bool missing: false
    property bool updateSuggested: false
    /// yt-dlp is being installed or updated, to look again after.
    property bool installing: false
    /// What to get: 0 for the best picture, a height, or "audio".
    property var get: 0
    /// Advanced: a video's container, the sound's format.
    property bool advanced: false
    property string container: "mp4"
    property string audioFormat: "m4a"

    readonly property YtdlpUpdater updater: root.session ? root.session.tools.ytdlp : null
    readonly property bool audioOnly: root.get === "audio"

    /// A download started: ``requestId`` reports through
    /// session.download.downloadFinished.
    signal started(var requestId)

    /// Open empty, getting the sound alone when ``audio`` (from Audio).
    function openDialog(audio) {
        root.url = ""
        root.info = null
        root.error = ""
        root.missing = false
        root.updateSuggested = false
        root.looking = false
        root.installing = false
        root.lookupId = -1
        root.get = audio ? "audio" : 0
        root.advanced = false
        root.container = "mp4"
        root.audioFormat = "m4a"
        root.open = true
    }

    function lookUp() {
        if (!root.session || !root.url.trim().length || root.looking)
            return
        root.info = null
        root.error = ""
        root.missing = false
        root.updateSuggested = false
        root.looking = true
        root.lookupId = root.session.download.lookupUrl(root.url)
    }

    /// Install or update yt-dlp, then look the link up again.
    function fixYtdlp() {
        root.error = ""
        root.installing = true
        root.updater.update()
    }

    /// Best, each smaller height, then Audio only.
    function getOptions() {
        if (!root.info)
            return []
        const heights = root.info.heights || []
        const out = [{
            value: 0,
            label: heights.length ? qsTr("Best (%1p)").arg(heights[0]) : qsTr("Best available")
        }]
        const smaller = root.info.playlist ? [1080, 720, 480] : heights.slice(1)
        for (const h of smaller)
            out.push({ value: h, label: qsTr("%1p").arg(h) })
        out.push({ value: "audio", label: qsTr("Audio only") })
        return out
    }

    Connections {
        target: root.session ? root.session.download : null
        function onLookupFinished(requestId, result) {
            if (requestId !== root.lookupId)
                return
            root.lookupId = -1
            root.looking = false
            if (!result.ok) {
                root.error = result.error
                root.missing = !!result.missing
                root.updateSuggested = !!result.updateSuggested
                return
            }
            root.info = result
            // Sound alone has no picture to choose.
            if (!result.hasVideo && !result.playlist)
                root.get = "audio"
        }
    }
    Connections {
        target: root.updater
        function onFinished(ok, message) {
            if (!root.installing)
                return
            root.installing = false
            if (ok)
                root.lookUp()
            else
                root.error = message
        }
    }

    title: qsTr("Download from the internet")
    blurb: qsTr("Paste a link to a video or song from a supported site, then look it up.")
    cardWidth: 500
    danger: false
    acceptLabel: qsTr("Download")
    acceptEnabled: !!root.info && !root.looking && !root.installing
    onAccepted: {
        const id = root.session.download.downloadUrl(root.url, {
            audioOnly: root.audioOnly,
            maxHeight: root.audioOnly ? 0 : root.get,
            playlist: !!root.info.playlist,
            container: root.container,
            audioFormat: root.audioFormat
        }, root.info.title, root.videoFolderId, root.audioFolderId)
        root.started(id)
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        DialogField {
            Layout.fillWidth: true
            text: root.url
            placeholder: qsTr("https://")
            onEdited: function (v) {
                root.url = v
                // A different link: what was looked up is not its.
                root.info = null
                root.error = ""
                root.missing = false
                root.updateSuggested = false
            }
            onSubmitted: root.lookUp()
        }
        AppButton {
            label: qsTr("Look up")
            enabled: root.url.trim().length > 0 && !root.looking && !root.installing
            onClicked: root.lookUp()
        }
    }

    // How the lookup is going, or why it did not work.
    Text {
        Layout.fillWidth: true
        visible: root.looking || root.installing
        text: root.installing ? root.updater.status : qsTr("Looking up…")
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
    }
    Text {
        Layout.fillWidth: true
        visible: root.error.length > 0 && !root.installing
        text: root.error
        color: Style.textDanger
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: (root.missing || root.updateSuggested) && !root.installing && !!root.updater
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: root.missing
                  ? qsTr("SeventhSlide can install its own copy of yt-dlp, and keep it up to date.")
                  : qsTr("Sites change often, and a newer yt-dlp usually follows them.")
            color: Style.textCaption
            font.pixelSize: Style.fontSmall
            wrapMode: Text.Wrap
        }
        AppButton {
            label: root.missing ? qsTr("Install yt-dlp") : qsTr("Update and Try Again")
            enabled: !!root.updater && !root.updater.busy
            onClicked: root.fixYtdlp()
        }
    }

    // What the link holds, and what to get of it.
    GroupList {
        Layout.fillWidth: true
        Layout.topMargin: 4
        visible: !!root.info

        // The title over who posted it, and its length at the right,
        // centred on the two.
        GroupRow {
            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: root.info ? root.info.title : ""
                    color: Style.textStrong
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: {
                        if (!root.info)
                            return ""
                        if (root.info.playlist)
                            return qsTr("A playlist of %1 items").arg((root.info.entries || []).length)
                        return root.info.uploader || ""
                    }
                    color: Style.textCaption
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
            }
            Text {
                Layout.leftMargin: 12
                Layout.alignment: Qt.AlignVCenter
                visible: text.length > 0
                text: root.info && !root.info.playlist
                      ? ConvertText.summary({ duration: root.info.duration || 0 }) : ""
                color: Style.textSoft
                font.pixelSize: Style.fontBody
                font.features: { "tnum": 1 }
            }
        }
        SettingRow {
            label: qsTr("Get")
            OptionField {
                options: root.getOptions()
                value: root.get
                onPicked: v => root.get = v
            }
        }
        // Advanced: what the file comes as.
        GroupDisclosureRow {
            label: qsTr("Advanced")
            open: root.advanced
            onClicked: root.advanced = !root.advanced
        }
        SettingRow {
            visible: root.advanced && !root.audioOnly
            label: qsTr("Container")
            OptionField {
                options: [{ value: "mp4", label: qsTr("MP4") },
                          { value: "webm", label: qsTr("WebM") },
                          { value: "mkv", label: qsTr("MKV") }]
                value: root.container
                onPicked: v => root.container = v
            }
        }
        SettingRow {
            visible: root.advanced && root.audioOnly
            label: qsTr("Audio format")
            OptionField {
                options: [{ value: "m4a", label: qsTr("M4A") },
                          { value: "mp3", label: qsTr("MP3") },
                          { value: "opus", label: qsTr("Opus") },
                          { value: "flac", label: qsTr("FLAC") },
                          { value: "wav", label: qsTr("WAV") }]
                value: root.audioFormat
                onPicked: v => root.audioFormat = v
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: qsTr("Only download media your organization has the right to use.")
        color: Style.textFaint
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }
}
