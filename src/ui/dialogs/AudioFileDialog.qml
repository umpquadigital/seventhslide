pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// An audio file in the Library, edited: its name (the extension kept) and
// the fades it sets for itself wherever it plays. A fade left empty follows
// the open service's, then each output's. What Save does is the caller's
// (``saved``).
AppDialog {
    id: root

    /// The file edited (its name in the library's audio folder).
    property string filename: ""
    /// Its title without the extension, as typed, and the extension kept.
    property string stem: ""
    property string extension: ""
    /// As typed: {fade_in_ms, fade_out_ms, overlap_ms}, each "" for none of
    /// its own, and on_play_pause "on", "off" or "" (none of its own).
    property var fades: ({ fade_in_ms: "", fade_out_ms: "", overlap_ms: "",
                           on_play_pause: "" })

    /// Save: the title typed (without the extension), and the fades typed
    /// as numbers, only those set.
    signal saved(string stem, var fades)

    /// Open on ``filename``, titled ``stem`` + ``extension``, with ``own``
    /// ({key: ms}, the fields it sets).
    function openFor(filename, stem, extension, own) {
        root.filename = filename
        root.stem = stem
        root.extension = extension
        const shown = function (key) {
            return own && own[key] !== undefined ? String(own[key]) : ""
        }
        const onPlayPause = own && own.on_play_pause !== undefined
                            ? (own.on_play_pause ? "on" : "off") : ""
        root.fades = { fade_in_ms: shown("fade_in_ms"), fade_out_ms: shown("fade_out_ms"),
                       overlap_ms: shown("overlap_ms"), on_play_pause: onPlayPause }
        root.open = true
    }

    function setFade(key, text) {
        const next = Object.assign({}, root.fades)
        next[key] = text
        root.fades = next
    }

    /// The fades typed, as numbers: only those set.
    function typed() {
        const out = {}
        for (const key of ["fade_in_ms", "fade_out_ms", "overlap_ms"]) {
            const n = parseInt(root.fades[key], 10)
            if (!isNaN(n))
                out[key] = n
        }
        if (root.fades.on_play_pause.length)
            out.on_play_pause = root.fades.on_play_pause === "on"
        return out
    }

    title: qsTr("Edit audio file")
    cardWidth: 460
    danger: false
    acceptLabel: qsTr("Save")
    acceptEnabled: root.stem.trim().length > 0
    onAccepted: root.saved(root.stem.trim(), root.typed())

    GroupList {
        Layout.fillWidth: true

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Name")
                RowLayout {
                    spacing: 4
                    DialogField {
                        objectName: "audioFileName"
                        implicitWidth: Style.fieldLongW
                        text: root.stem
                        onEdited: v => root.stem = v
                        onSubmitted: root.submit()
                    }
                    Text {
                        visible: root.extension.length > 0
                        text: root.extension
                        color: Style.textMuted
                        font.pixelSize: Style.fontBody
                    }
                }
            }
        }
        Repeater {
            model: [
                { key: "fade_in_ms", label: qsTr("Fade in") },
                { key: "fade_out_ms", label: qsTr("Fade out") },
                { key: "overlap_ms", label: qsTr("Overlap") }
            ]
            delegate: GroupRow {
                id: fadeRow
                required property var modelData
                FormRow {
                    Layout.fillWidth: true
                    label: fadeRow.modelData.label
                    FadeField {
                        objectName: "audioFile_" + fadeRow.modelData.key
                        ms: root.fades[fadeRow.modelData.key]
                        placeholder: qsTr("Default")
                        onEdited: function (v) { root.setFade(fadeRow.modelData.key, v) }
                        onSubmitted: root.submit()
                    }
                }
            }
        }
        SettingRow {
            label: qsTr("Fades apply to play/pause")
            OptionField {
                objectName: "audioFile_on_play_pause"
                implicitWidth: Style.fieldShortW
                options: [
                    { value: "", label: qsTr("Default") },
                    { value: "on", label: qsTr("On") },
                    { value: "off", label: qsTr("Off") }
                ]
                value: root.fades.on_play_pause
                onPicked: function (v) { root.setFade("on_play_pause", v) }
            }
        }
    }
}
