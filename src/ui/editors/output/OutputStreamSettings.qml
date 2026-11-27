pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// An SRT destination's Stream settings: who connects to whom, where, and how
// the stream trades delay for reliability. A handful of choices; the
// streamer turns each into its encoder and SRT settings (stream_settings.hpp).
// Each change goes to the host at once, as the other destination settings do.
Column {
    id: stream

    required property OutputDestinationsState dialog
    required property var dest
    /// The width the panel gives each control.
    property real controlW: Style.fieldMediumW

    readonly property var settings: (stream.dest && stream.dest.stream) || ({})
    readonly property bool listening: stream.settings.mode !== "caller"
    /// Encryption switched on but no passphrase given yet.
    property bool encryptPending: false

    function change(changes) {
        if (stream.dest)
            stream.dialog.setDestinationStream(stream.dest.id, changes)
    }

    // Per destination: a status update reloads the row, and must not undo
    // a switch the operator has just turned on.
    readonly property string destId: (stream.dest && stream.dest.id) || ""
    onDestIdChanged: stream.encryptPending = false

    GroupHeading {
        text: qsTr("Stream")
    }
    GroupList {
        width: parent.width

        SettingRow {
            label: qsTr("Connection")
            OptionField {
                width: stream.controlW
                value: stream.settings.mode
                options: [
                    { value: "listener", label: qsTr("Wait for a receiver") },
                    { value: "caller", label: qsTr("Send to a receiver") }
                ]
                onPicked: function (v) { stream.change({ mode: v }) }
            }
        }
        SettingRow {
            visible: !stream.listening
            label: qsTr("Address")
            DialogField {
                width: stream.controlW
                text: stream.settings.host || ""
                placeholder: qsTr("receiver.local or 192.168.1.20")
                onCommitted: function (v) {
                    if (v !== (stream.settings.host || ""))
                        stream.change({ host: v })
                }
            }
        }
        SettingRow {
            label: qsTr("Port")
            DialogField {
                width: Style.fieldShortW
                text: stream.settings.port !== undefined ? String(stream.settings.port) : ""
                maximumLength: 5
                onCommitted: function (v) {
                    if (v !== String(stream.settings.port))
                        stream.change({ port: v })
                }
            }
        }
        SettingRow {
            label: qsTr("Quality")
            OptionField {
                width: stream.controlW
                value: stream.settings.preset
                options: [
                    { value: "low_delay", label: qsTr("Low delay"), note: qsTr("Local network") },
                    { value: "reliable", label: qsTr("Reliable"), note: qsTr("Internet") }
                ]
                onPicked: function (v) { stream.change({ preset: v }) }
            }
        }
        SettingRow {
            label: qsTr("Video")
            OptionField {
                // Said in the field while this computer's encoders are
                // tried, so nothing below moves when they are known.
                readonly property bool checking: stream.dialog.streamCodecs.length === 0
                width: stream.controlW
                enabled: !checking
                value: checking ? "" : stream.settings.codec
                options: checking ? [{ value: "", label: qsTr("Checking…") }]
                                  : stream.dialog.streamCodecOptions()
                onPicked: function (v) { stream.change({ codec: v }) }
            }
        }
        SettingRow {
            label: qsTr("Bitrate")
            OptionField {
                width: stream.controlW
                value: stream.settings.bitrateKbps || 0
                options: stream.dialog.streamBitrateOptions(stream.dest)
                onPicked: function (v) { stream.change({ bitrateKbps: v }) }
            }
        }
        SettingRow {
            label: qsTr("Encryption")
            ToggleSwitch {
                checked: !!stream.settings.encrypted || stream.encryptPending
                onToggled: {
                    if (stream.settings.encrypted) {
                        stream.encryptPending = false
                        stream.change({ passphrase: "" })
                    } else {
                        stream.encryptPending = !stream.encryptPending
                    }
                }
            }
        }
        // The passphrase is never shown back: typing one replaces it.
        SettingRow {
            visible: !!stream.settings.encrypted || stream.encryptPending
            label: qsTr("Passphrase")
            DialogField {
                width: stream.controlW
                password: true
                placeholder: stream.settings.encrypted ? qsTr("Set; type to change it")
                                                       : qsTr("10 to 79 characters")
                onCommitted: function (v) {
                    if (v.length > 0)
                        stream.change({ passphrase: v })
                }
            }
        }
    }
}
