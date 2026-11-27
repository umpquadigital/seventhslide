pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One channel strip of the Audio Mixer, as OBS's vertical mixer draws one:
// the stack's or output's glyph and name, its level in dB, the fader beside
// the meter and its scale, and Mute and Listen. A muted strip's meter goes
// grey; it still reads, so the operator can see what Mute holds back.
Item {
    id: strip

    required property MixerController mixer
    /// "stack" or "output".
    required property string kind
    required property string channelId
    required property string name
    required property string icon
    required property string gainText
    required property real faderPosition
    required property bool muted
    required property bool listen
    /// A change the mixer refused, for the panel to show.
    signal failed(string message)

    implicitWidth: 70

    function report(error) {
        if (error.length)
            strip.failed(error)
    }

    // The hairline between strips.
    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Style.wellEdge
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 7
        anchors.topMargin: 8
        anchors.bottomMargin: 8
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Image {
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
                visible: strip.icon.length > 0
                source: strip.icon.length ? Icons.url(strip.icon, Style.iconIdle, 2) : ""
                sourceSize: Qt.size(12, 12)
            }
            Text {
                Layout.fillWidth: true
                text: strip.name
                color: Style.text
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
            }
        }

        Text {
            Layout.fillWidth: true
            text: strip.gainText
            color: strip.muted ? Style.textFaint : Style.textMuted
            font.pixelSize: Style.fontCaption
            horizontalAlignment: Text.AlignHCenter
        }

        // Scale, meter and fader, side by side.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 60

            // The fader's handle centre and the meter's ends line up: both
            // run the same height, half a handle in from the top and bottom.
            readonly property real inset: fader.handleHeight / 2

            Item {
                id: scale
                anchors.left: parent.left
                width: 18
                y: parent.inset
                height: parent.height - 2 * parent.inset
                Repeater {
                    model: [0, -10, -20, -30, -40, -50, -60]
                    delegate: Text {
                        required property int modelData
                        // By the scale, not parent: a Repeater's delegate can
                        // be bound while it has no parent (seen with Qt 6.12),
                        // and an anchor to parent.right then reads null.
                        x: scale.width - width
                        y: (1 - strip.mixer.meterPosition(modelData)) * scale.height - height / 2
                        text: modelData === 0 ? "0" : String(-modelData)
                        color: Style.meterTick
                        font.pixelSize: Style.fontMicro
                    }
                }
            }

            AudioMeterItem {
                id: meter
                anchors.left: scale.right
                anchors.leftMargin: 3
                y: parent.inset
                width: 9
                height: parent.height - 2 * parent.inset
                mixer: strip.mixer
                kind: strip.kind
                channelId: strip.channelId
                dimmed: strip.muted
                trackColor: Style.meterTrack
                lowColor: Style.meterGreen
                midColor: Style.meterYellow
                highColor: Style.meterRed
                mutedColor: Style.meterMuted
            }

            VerticalFader {
                id: fader
                anchors.left: meter.right
                anchors.leftMargin: 6
                anchors.right: parent.right
                height: parent.height
                position: strip.faderPosition
                onMoved: position => strip.report(
                             strip.mixer.setGainDb(strip.kind, strip.channelId,
                                                   strip.mixer.faderDb(position)))
                onReset: strip.report(strip.mixer.setGainDb(strip.kind, strip.channelId, 0))
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 4
            IconToolButton {
                size: "compact"
                icon: strip.muted ? "speakerMuted" : "audio"
                danger: strip.muted
                onClicked: strip.report(strip.mixer.setMuted(strip.kind, strip.channelId,
                                                             !strip.muted))
            }
            IconToolButton {
                size: "compact"
                icon: "headphones"
                toggled: strip.listen
                toggledKind: "on"
                onClicked: strip.report(strip.mixer.setListen(strip.kind, strip.channelId,
                                                              !strip.listen))
            }
        }
    }
}
