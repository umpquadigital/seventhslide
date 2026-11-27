pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Convert: the choices for re-encoding library files of one kind - a video's
// size (a preset, or an exact one that keeps its shape), format, quality and
// frame rate; an audio file's format and bit rate; an image's format and
// size. A video can become sound alone, which goes to Audio. Advanced holds
// the codecs, and a sound's sample rate and channels; it offers only what the
// format can hold. Then the new file's name, and whether it replaces the
// original once it is done.
//
// ``openFor`` opens it on the files picked; ``probe`` arrives when ffprobe
// has read the first of them (``probing`` until then). Convert asks with
// ``convertRequested``: the choice as media::convert_choice_from_json reads
// it, Replace original, the name (empty: from what changed), and what kind
// of file it makes.
AppDialog {
    id: root

    /// ``video``, ``audio`` or ``image``.
    property string kind: "video"
    /// The files picked: ``[{title, filename}]``.
    property var sources: []
    /// What ffprobe read from the first: see ConvertText.
    property var probe: ({})
    property bool probing: false
    property string probeError: ""

    // The choice.
    property string format: "mp4"
    property int targetHeight: 0
    /// An exact size typed in, instead of a preset.
    property bool customSize: false
    property int customWidth: 0
    property int customHeight: 0
    property string quality: "balanced"
    property int fps: 0
    /// A sound's bit rate (sound alone), or a video's sound's.
    property int bitrate: 0
    property string videoCodec: "auto"
    property string audioCodec: "auto"
    property int sampleRate: 0
    property int channels: 0
    property bool advanced: false
    property bool replaceOriginal: false
    /// The name typed in; until then the name follows the choices.
    property string name: ""
    property bool nameEdited: false

    readonly property bool single: root.sources.length === 1
    readonly property var first: root.sources.length ? root.sources[0] : ({ title: "", filename: "" })
    readonly property int sourceWidth: root.single ? (root.probe.width || 0) : 0
    readonly property int sourceHeight: root.single ? (root.probe.height || 0) : 0
    /// The shorter side, which names a size: 1080 for 1080 × 1920.
    readonly property int sourceShort: root.sourceWidth > 0 && root.sourceHeight > 0
                                       ? Math.min(root.sourceWidth, root.sourceHeight) : 0
    /// What the choice makes: ``video``, ``audio`` or ``image``. The format
    /// alone decides it; the whole ``choice`` reads ``keepsPicture``, which
    /// reads this.
    readonly property string outKind: ConvertText.outputKind(root.kind, { format: root.format })
                                      || root.kind
    readonly property bool makesVideo: root.outKind === "video"
    readonly property bool makesAudio: root.outKind === "audio"
    readonly property bool keepsPicture: root.makesVideo && root.videoCodec === "copy"
    readonly property var lossy: ["aac", "mp3", "opus", "vorbis"]
    /// The sound's bit rate matters: a lossy codec, chosen or the format's
    /// own.
    readonly property bool soundIsLossy: {
        if (root.makesAudio) {
            if (root.audioCodec === "auto")
                return ["mp3", "m4a", "ogg"].indexOf(root.format) >= 0
            return root.lossy.indexOf(root.audioCodec) >= 0
        }
        if (root.audioCodec === "auto")
            return root.format !== "mov"
        return root.lossy.indexOf(root.audioCodec) >= 0
    }

    readonly property var choice: {
        const height = root.customSize ? root.customHeight : root.targetHeight
        if (root.kind === "image")
            return { format: root.format, height: height, exactHeight: root.customSize }
        if (root.kind === "audio" || ["mp3", "m4a", "wav", "flac", "ogg"].indexOf(root.format) >= 0)
            return { format: root.format, bitrate: root.soundIsLossy ? root.bitrate : 0,
                     codec: root.audioCodec, sampleRate: root.sampleRate,
                     channels: root.channels }
        return { format: root.format, height: root.keepsPicture ? 0 : height,
                 exactHeight: root.customSize && !root.keepsPicture, quality: root.quality,
                 fps: root.keepsPicture ? 0 : root.fps, videoCodec: root.videoCodec,
                 audioCodec: root.audioCodec, audioBitrate: root.soundIsLossy ? root.bitrate : 0 }
    }

    /// The new file's extension, and the name the choices give it.
    readonly property string extension: ConvertText.extension(root.kind, root.choice)
    readonly property string autoTitle: ConvertText.newTitle(root.kind, root.first.title,
                                                             root.first.filename, root.choice,
                                                             root.probe,
                                                             root.replaceOriginal && root.sameKind)
    readonly property string autoName: root.autoTitle.length >= root.extension.length
                                       && root.autoTitle.endsWith(root.extension)
                                       ? root.autoTitle.slice(0, root.autoTitle.length
                                                                 - root.extension.length)
                                       : root.autoTitle
    readonly property string shownName: root.nameEdited ? root.name : root.autoName
    readonly property bool sameKind: root.outKind === root.kind
    readonly property string sizeText: root.single
        ? ConvertText.estimatedSize(root.kind, root.choice, root.probe) : ""
    /// The picture the card shows: a video's frame (its thumbnail until the
    /// probe brings one), an image's thumbnail; none for sound.
    readonly property string thumbSource: root.probe.frame || root.first.thumb || ""
    /// The file's codecs and channels, as the card's tags.
    readonly property var formatTags: root.single && !root.probing && !root.probeError.length
                                      ? ConvertText.formatTags(root.first.filename, root.probe)
                                      : []
    /// What the file is, as the card lists it.
    readonly property var facts: root.single && !root.probing && !root.probeError.length
                                 ? ConvertText.details(root.kind, root.probe) : []

    signal convertRequested(var choice, bool replaceOriginal, string name, string outKind)

    /// Open on ``files`` (``[{title, filename, thumb}]``, ``thumb`` its library
    /// thumbnail URL, if it has one) of ``kind``, at that kind's defaults, waiting for their
    /// probe.
    function openFor(kind, files) {
        root.kind = kind
        root.sources = files
        root.probe = ({})
        root.probing = true
        root.probeError = ""
        root.format = kind === "video" ? "mp4" : kind === "audio" ? "mp3" : "jpg"
        root.targetHeight = 0
        root.customSize = false
        root.customWidth = 0
        root.customHeight = 0
        root.quality = "balanced"
        root.fps = 0
        root.bitrate = 0
        root.videoCodec = "auto"
        root.audioCodec = "auto"
        root.sampleRate = 0
        root.channels = 0
        root.advanced = false
        root.replaceOriginal = false
        root.name = ""
        root.nameEdited = false
        root.open = true
    }

    /// What ffprobe read, or why it could not.
    function setProbe(probe, error) {
        root.probe = probe || ({})
        root.probeError = error || ""
        root.probing = false
    }

    /// A new format: codecs it cannot hold go back to its own.
    function setFormat(f) {
        root.format = f
        if (ConvertText.videoCodecs(f).indexOf(root.videoCodec) < 0)
            root.videoCodec = "auto"
        if (ConvertText.audioCodecs(f).indexOf(root.audioCodec) < 0)
            root.audioCodec = "auto"
    }

    /// An even number, as video codecs want sizes.
    function even(n) {
        return root.kind === "video" ? n - (n % 2) : n
    }

    /// Start the exact size at the source's own.
    function chooseCustom() {
        root.customSize = true
        root.customHeight = root.sourceHeight > 0 ? root.sourceHeight : 1080
        root.customWidth = root.sourceHeight > 0 ? root.sourceWidth
                                                 : Math.round(root.customHeight * 16 / 9)
    }

    /// The width typed in, the height following in the picture's shape.
    function setCustomWidth(w) {
        if (!(w > 0))
            return
        root.customWidth = w
        if (root.sourceWidth > 0)
            root.customHeight = root.even(Math.round(w * root.sourceHeight / root.sourceWidth))
    }

    function setCustomHeight(h) {
        if (!(h > 0))
            return
        root.customHeight = root.even(h)
        if (root.sourceHeight > 0)
            root.customWidth = root.even(Math.round(root.customHeight * root.sourceWidth
                                                    / root.sourceHeight))
    }

    /// The heights offered: Keep, each below the source's (every one when
    /// several files are picked, each kept at its own size if smaller), then
    /// Custom.
    function heightOptions() {
        // A preset names the shorter side: an upright 1080 × 1920 video is
        // 1080p, as a wide one is.
        const keep = root.sourceShort > 0
            ? (root.kind === "video" ? qsTr("Keep (%1p)").arg(root.sourceShort)
                                     : qsTr("Keep (%1 × %2)").arg(root.sourceWidth)
                                           .arg(root.sourceHeight))
            : qsTr("Keep")
        const out = [{ value: 0, label: keep }]
        for (const h of ConvertText.heights) {
            if (root.sourceShort > 0 && h >= root.sourceShort)
                continue
            let label = qsTr("%1p").arg(h)
            if (root.kind === "image")
                label = ConvertText.scaledSize(h, root.probe) || qsTr("%1p").arg(h)
            out.push({ value: h, label: label })
        }
        out.push({ value: -1, label: qsTr("Custom…") })
        return out
    }

    function formatOptions() {
        const out = [...ConvertText.formats(root.kind)]
        // Sound alone, to Audio - when there is sound to keep.
        if (root.kind === "video" && (!root.single || root.probing || !!root.probe.hasAudio))
            out.push(...ConvertText.formats("audio"))
        return out
    }

    function fpsOptions() {
        const rate = root.single && root.probe.fps > 0
            ? Number(root.probe.fps.toPrecision(4)) : 0
        const out = [{ value: 0, label: rate > 0 ? qsTr("Keep (%1 fps)").arg(rate) : qsTr("Keep") }]
        for (const r of ConvertText.frameRates)
            out.push({ value: r, label: qsTr("%1 fps").arg(r) })
        return out
    }

    function bitrateOptions() {
        const out = [{ value: 0, label: qsTr("Standard") }]
        for (const k of ConvertText.audioBitrates)
            out.push({ value: k, label: qsTr("%1 kbps").arg(k) })
        return out
    }

    readonly property var codecLabels: ({
        auto: qsTr("Automatic"), copy: qsTr("Keep as is"),
        h264: qsTr("H.264"), h265: qsTr("H.265 (HEVC)"), vp9: qsTr("VP9"), av1: qsTr("AV1"),
        prores: qsTr("ProRes"),
        aac: qsTr("AAC"), mp3: qsTr("MP3"), opus: qsTr("Opus"), vorbis: qsTr("Vorbis"),
        flac: qsTr("FLAC"), pcm: qsTr("PCM, uncompressed"), alac: qsTr("ALAC, lossless")
    })

    function codecOptions(keys) {
        return keys.map(k => ({ value: k, label: root.codecLabels[k] || k }))
    }

    title: {
        const many = root.sources.length
        if (root.kind === "video")
            return many > 1 ? qsTr("Convert %1 videos").arg(many) : qsTr("Convert video")
        if (root.kind === "audio")
            return many > 1 ? qsTr("Convert %1 audio files").arg(many) : qsTr("Convert audio")
        return many > 1 ? qsTr("Convert %1 images").arg(many) : qsTr("Convert image")
    }
    cardWidth: 480
    danger: false
    acceptLabel: qsTr("Convert")
    acceptEnabled: !root.probing && !root.probeError.length && root.extension.length > 0
                   && (!root.single || root.shownName.trim().length > 0)
                   && (!root.customSize || root.customHeight >= 16)
    onAccepted: root.convertRequested(root.choice, root.replaceOriginal && root.sameKind,
                                      root.single && root.nameEdited ? root.name.trim() : "",
                                      root.outKind)

    // The file: its picture, its name and what it is.
    Rectangle {
        Layout.fillWidth: true
        Layout.bottomMargin: 4
        implicitHeight: card.implicitHeight + 24
        radius: Style.radiusLarge
        color: Style.groupBg
        border.color: Style.groupEdge

        ColumnLayout {
            id: card
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 54
                    radius: Style.radiusSmall
                    color: Style.thumbWell
                    clip: true
                    Image {
                        anchors.fill: parent
                        visible: root.thumbSource.length > 0
                        source: root.thumbSource
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize: Qt.size(192, 108)
                    }
                    Image {
                        anchors.centerIn: parent
                        visible: !root.thumbSource.length
                        width: 22
                        height: 22
                        source: Icons.url(root.kind, Style.textCaption)
                        sourceSize: Qt.size(22, 22)
                    }
                    // Several files: how many, over the first one's picture.
                    Rectangle {
                        visible: !root.single
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 4
                        width: Math.max(18, countText.implicitWidth + 10)
                        height: 18
                        radius: 9
                        color: Style.accent
                        BadgeText {
                            id: countText
                            anchors.centerIn: parent
                            text: String(root.sources.length)
                            color: Style.textStrong
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.DemiBold
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: {
                            if (root.single)
                                return root.first.title
                            const n = root.sources.length
                            return root.kind === "video" ? qsTr("%1 videos").arg(n)
                                 : root.kind === "audio" ? qsTr("%1 audio files").arg(n)
                                 : qsTr("%1 images").arg(n)
                        }
                        color: Style.textStrong
                        font.pixelSize: Style.fontBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideMiddle
                    }
                    // Its codecs and channels, each a tag of its own.
                    Flow {
                        Layout.fillWidth: true
                        visible: root.formatTags.length > 0
                        spacing: 4
                        Repeater {
                            model: root.formatTags
                            delegate: Rectangle {
                                required property string modelData
                                implicitWidth: tagText.implicitWidth + 12
                                implicitHeight: tagText.implicitHeight + 4
                                radius: Style.radiusSmall
                                color: Style.thumbWell
                                border.color: Style.groupEdge
                                Text {
                                    id: tagText
                                    anchors.centerIn: parent
                                    text: parent.modelData
                                    color: Style.textSoft
                                    font.pixelSize: Style.fontSmall
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.formatTags.length === 0
                        text: {
                            if (!root.single)
                                return root.sources.map(s => s.title).join(", ")
                            if (root.probing)
                                return qsTr("Reading the file…")
                            if (root.probeError.length)
                                return qsTr("Could not be read: %1").arg(root.probeError)
                            return ""
                        }
                        color: root.probeError.length ? Style.textDanger : Style.textCaption
                        font.pixelSize: Style.fontSmall
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }

            // What the file is, in columns: the label small over its value.
            Rectangle {
                visible: root.facts.length > 0
                Layout.fillWidth: true
                implicitHeight: 1
                color: Style.groupRule
            }
            RowLayout {
                visible: root.facts.length > 0
                Layout.fillWidth: true
                spacing: 0
                Repeater {
                    model: root.facts
                    delegate: Item {
                        id: fact
                        required property var modelData
                        required property int index
                        // As wide as it needs, then a share of what is left.
                        Layout.fillWidth: true
                        Layout.preferredWidth: factCol.implicitWidth
                                               + (fact.index > 0 ? 10 : 0) + 6
                        // Every column as tall as the tallest, read from
                        // the top, so labels line up and rules run full.
                        Layout.fillHeight: true
                        implicitHeight: factCol.implicitHeight
                        Rectangle {
                            visible: fact.index > 0
                            width: 1
                            height: parent.height
                            color: Style.groupRule
                        }
                        ColumnLayout {
                            id: factCol
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: fact.index > 0 ? 10 : 0
                            anchors.rightMargin: 6
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: fact.modelData.label
                                color: Style.textCaption
                                font.pixelSize: Style.fontSmall
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: fact.modelData.value
                                color: Style.textStrong
                                font.pixelSize: Style.fontBody
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }

    GroupList {
        Layout.fillWidth: true

        SettingRow {
            visible: !root.makesAudio
            label: qsTr("Size")
            OptionField {
                enabled: !root.keepsPicture
                options: root.heightOptions()
                value: root.customSize ? -1 : root.targetHeight
                onPicked: function (v) {
                    if (v === -1) {
                        root.chooseCustom()
                    } else {
                        root.customSize = false
                        root.targetHeight = v
                    }
                }
            }
        }
        // The exact size: one side typed in, the other following the
        // picture's shape. Several files keep their own shapes: height only.
        GroupRow {
            visible: !root.makesAudio && root.customSize && !root.keepsPicture
            FormRow {
                Layout.fillWidth: true
                label: root.single ? qsTr("Width & Height") : qsTr("Height")
                RowLayout {
                    spacing: 6
                    DialogField {
                        visible: root.single
                        implicitWidth: 64
                        text: String(root.customWidth)
                        onEdited: v => root.setCustomWidth(parseInt(v, 10))
                    }
                    Text {
                        visible: root.single
                        text: "×"
                        color: Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                    DialogField {
                        implicitWidth: 64
                        text: String(root.customHeight)
                        onEdited: v => root.setCustomHeight(parseInt(v, 10))
                    }
                    Text {
                        text: qsTr("px")
                        color: Style.textCaption
                        font.pixelSize: Style.fontSmall
                    }
                }
            }
        }
        SettingRow {
            label: qsTr("Format")
            OptionField {
                options: root.formatOptions()
                value: root.format
                onPicked: v => root.setFormat(v)
            }
        }
        SettingRow {
            visible: root.makesVideo
            label: qsTr("Quality")
            SegmentedControl {
                enabled: !root.keepsPicture
                opacity: enabled ? 1 : 0.4
                options: [qsTr("Smaller"), qsTr("Balanced"), qsTr("Better")]
                current: ["smaller", "balanced", "better"].indexOf(root.quality)
                onChosen: i => root.quality = ["smaller", "balanced", "better"][i]
            }
        }
        SettingRow {
            visible: root.makesVideo
            label: qsTr("Frame rate")
            OptionField {
                enabled: !root.keepsPicture
                options: root.fpsOptions()
                value: root.fps
                onPicked: v => root.fps = v
            }
        }
        SettingRow {
            visible: root.makesAudio && root.soundIsLossy
            label: qsTr("Bit rate")
            OptionField {
                options: root.bitrateOptions()
                value: root.bitrate
                onPicked: v => root.bitrate = v
            }
        }

        // Advanced: the codecs, and a sound's sample rate and channels.
        GroupDisclosureRow {
            visible: root.kind !== "image"
            label: qsTr("Advanced")
            open: root.advanced
            onClicked: root.advanced = !root.advanced
        }
        SettingRow {
            visible: root.advanced && root.makesVideo
            label: qsTr("Video codec")
            OptionField {
                options: root.codecOptions(ConvertText.videoCodecs(root.format))
                value: root.videoCodec
                onPicked: function (v) {
                    root.videoCodec = v
                    if (v === "copy") {
                        root.customSize = false
                        root.targetHeight = 0
                        root.fps = 0
                    }
                }
            }
        }
        SettingRow {
            visible: root.advanced && root.kind !== "image"
            label: root.makesVideo ? qsTr("Audio codec") : qsTr("Codec")
            OptionField {
                options: root.codecOptions(ConvertText.audioCodecs(root.format))
                value: root.audioCodec
                onPicked: v => root.audioCodec = v
            }
        }
        SettingRow {
            visible: root.advanced && root.makesVideo && root.soundIsLossy
                     && root.audioCodec !== "copy"
            label: qsTr("Audio bit rate")
            OptionField {
                options: root.bitrateOptions()
                value: root.bitrate
                onPicked: v => root.bitrate = v
            }
        }
        SettingRow {
            visible: root.advanced && root.makesAudio && root.audioCodec !== "copy"
            label: qsTr("Sample rate")
            OptionField {
                options: [{ value: 0, label: qsTr("Keep") },
                          { value: 44100, label: qsTr("44.1 kHz") },
                          { value: 48000, label: qsTr("48 kHz") }]
                value: root.sampleRate
                onPicked: v => root.sampleRate = v
            }
        }
        SettingRow {
            visible: root.advanced && root.makesAudio && root.audioCodec !== "copy"
            label: qsTr("Channels")
            OptionField {
                options: [{ value: 0, label: qsTr("Keep") },
                          { value: 2, label: qsTr("Stereo") },
                          { value: 1, label: qsTr("Mono") }]
                value: root.channels
                onPicked: v => root.channels = v
            }
        }
    }

    GroupList {
        Layout.fillWidth: true
        Layout.topMargin: 4

        // One file's new name; several are each named for what changed.
        GroupRow {
            visible: root.single
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Name")
                hint: root.sizeText.length ? qsTr("About %1").arg(root.sizeText) : ""
                RowLayout {
                    spacing: 4
                    DialogField {
                        implicitWidth: Style.fieldLongW - 40
                        text: root.shownName
                        onEdited: function (v) {
                            root.name = v
                            root.nameEdited = true
                        }
                    }
                    Text {
                        Layout.preferredWidth: 36
                        text: root.extension
                        color: Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                }
            }
        }
        SettingRow {
            // Sound made from a video goes beside it, never in its place.
            visible: root.sameKind
            label: qsTr("Replace original")
            ToggleSwitch {
                checked: root.replaceOriginal
                onToggled: root.replaceOriginal = !root.replaceOriginal
            }
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: 2
        visible: !root.single && root.autoTitle.length > 0
        text: qsTr("Each is named for what changed, like “%1”.").arg(root.autoTitle)
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }
}
