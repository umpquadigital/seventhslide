pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// The controller's seek bar for live video and audio: the position, a slider
// and the length.
RowLayout {
    id: seekRow
    /// Untyped, since ControllerWell makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var well // ControllerWell
    spacing: 10
    Layout.fillWidth: true
    Layout.preferredHeight: 38
    visible: seekRow.well.mediaControls

    Text {
        id: positionLabel
        Layout.preferredWidth: Math.max(40, implicitWidth)
        horizontalAlignment: Text.AlignRight
        text: seekRow.well.formatMediaClock(
                  seekSlider.pressed ? seekSlider.value
                  : (seekRow.well.ready ? seekRow.well.view.mediaPositionSec : 0))
        color: Style.iconSoft
        font.pixelSize: Style.fontBody
        font.weight: Font.Medium
        font.features: { "tnum": 1 }
    }

    Slider {
        id: seekSlider
        Layout.fillWidth: true
        Layout.preferredHeight: 38
        from: 0
        to: {
            const d = seekRow.well.ready ? seekRow.well.view.mediaDurationSec : 0
            return d > 0.05 ? d : 1
        }
        enabled: !!(seekRow.well.ready && seekRow.well.view.mediaDurationSec > 0.05)
        live: true

        /// Where the last seek of this drag went, so letting go where it
        /// already is seeks no more.
        property real sentSec: -1

        /// Seek to ``sec`` unless the last seek went there. True when it did.
        function seekTo(sec) {
            if (!seekRow.well.ready || !seekSlider.enabled || sec === seekSlider.sentSec)
                return false
            seekSlider.sentSec = sec
            seekRow.well.view.mediaSeek(sec)
            return true
        }

        // The clock, except while the hand holds the handle: every playback
        // tick, a seek, and every change of media state (LiveController
        // forwards those as position changes too). Delayed, so a length that
        // arrives with the position reaches ``to`` first and the value is not
        // clamped to the old one. Let go of, the value stays where the hand
        // left it rather than going back to before.
        Binding {
            target: seekSlider
            property: "value"
            value: seekRow.well.ready ? seekRow.well.view.mediaPositionSec : 0
            when: !seekSlider.pressed
            delayed: true
            restoreMode: Binding.RestoreNone
        }

        // A drag seeks at once, then at most every scrubGap.interval: each
        // seek empties what the player decoded ahead, so one per mouse move
        // only stutters.
        Timer {
            id: scrubGap
            interval: 100
            onTriggered: {
                if (seekSlider.seekTo(seekSlider.value))
                    scrubGap.restart()
            }
        }

        onMoved: {
            seekRow.well.interacted()
            if (!scrubGap.running && seekTo(value))
                scrubGap.start()
        }
        onPressedChanged: {
            if (pressed) {
                seekRow.well.interacted()
                seekSlider.sentSec = value
            } else {
                scrubGap.stop()
                seekTo(value)
            }
        }

        background: Rectangle {
            x: seekSlider.leftPadding
            y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
            implicitWidth: 200
            implicitHeight: 5
            width: seekSlider.availableWidth
            height: implicitHeight
            radius: 2.5
            color: Style.sliderTrack
            border.color: Style.sliderTrackEdge

            Rectangle {
                width: seekSlider.visualPosition * parent.width
                height: parent.height
                radius: parent.radius
                color: Style.accent
            }
        }

        handle: Rectangle {
            x: seekSlider.leftPadding
               + seekSlider.visualPosition * (seekSlider.availableWidth - width)
            y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
            implicitWidth: 16
            implicitHeight: 16
            radius: height / 2
            color: seekSlider.pressed ? Style.sliderKnobPressed
                   : (seekSlider.hovered ? Style.sliderKnobHover : Style.sliderKnob)
            border.width: 1
            border.color: Style.sliderKnobEdge
        }
    }

    Text {
        Layout.preferredWidth: Math.max(40, implicitWidth)
        horizontalAlignment: Text.AlignLeft
        text: seekRow.well.formatMediaClock(seekRow.well.ready ? seekRow.well.view.mediaDurationSec : 0)
        color: Style.iconSoft
        font.pixelSize: Style.fontBody
        font.weight: Font.Medium
        font.features: { "tnum": 1 }
    }
}
