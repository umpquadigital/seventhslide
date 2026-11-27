import QtQuick
import SeventhSlide

// A vertical fader: a groove, lit from the bottom up to the handle, and the
// handle. ``position`` runs from 0 at the bottom to 1 at the top; the fader
// asks for a new one with ``moved`` and shows whatever it is given back.
//
// Drag the handle, or press the groove to jump there and drag on. The wheel
// steps it; a double-click asks for the top (``reset``). It takes no keys:
// the arrows and Page Up / Down always move the live output (Main.qml).
Item {
    id: fader

    property real position: 0
    /// One wheel notch.
    property real step: 0.02
    signal moved(real position)
    signal reset()

    readonly property int handleHeight: 10
    /// The handle's travel: its centre stays half a handle inside the ends.
    readonly property real travel: Math.max(1, fader.height - fader.handleHeight)
    readonly property real handleY: (1 - Math.max(0, Math.min(1, fader.position))) * fader.travel

    implicitWidth: 18

    function ask(next) {
        fader.moved(Math.max(0, Math.min(1, next)))
    }

    /// The position under ``y``, the handle's centre put there.
    function positionAt(y) {
        return 1 - (y - fader.handleHeight / 2) / fader.travel
    }

    // The groove, and what is lit of it.
    Rectangle {
        id: groove
        anchors.horizontalCenter: parent.horizontalCenter
        y: fader.handleHeight / 2
        width: 4
        height: fader.travel
        radius: 2
        color: Style.faderTrack
    }
    Rectangle {
        anchors.horizontalCenter: groove.horizontalCenter
        anchors.bottom: groove.bottom
        width: groove.width
        height: Math.max(0, groove.height - fader.handleY)
        radius: 2
        color: Style.faderFill
    }

    Rectangle {
        id: handle
        y: fader.handleY
        width: fader.width
        height: fader.handleHeight
        radius: Style.radiusSmall
        color: area.containsMouse || area.pressed ? Style.faderHandleHover : Style.faderHandle
        border.width: 1
        border.color: Style.faderHandleEdge
        // The cap's line, where the level is read.
        Rectangle {
            anchors.centerIn: parent
            width: parent.width - 6
            height: 1
            color: Style.faderHandleEdge
        }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        // In a list that scrolls sideways, a drag here is the fader's.
        preventStealing: true
        cursorShape: Qt.PointingHandCursor
        /// Where on the handle it was taken, so it does not jump.
        property real grab: 0

        onPressed: function (mouse) {
            const onHandle = mouse.y >= fader.handleY && mouse.y <= fader.handleY + fader.handleHeight
            area.grab = onHandle ? mouse.y - fader.handleY - fader.handleHeight / 2 : 0
            if (!onHandle)
                fader.ask(fader.positionAt(mouse.y))
        }
        onPositionChanged: function (mouse) {
            if (area.pressed)
                fader.ask(fader.positionAt(mouse.y - area.grab))
        }
        onDoubleClicked: fader.reset()
        onWheel: function (wheel) {
            // With natural scrolling (macOS's default, a trackpad's) the
            // system turns the delta round; turned back, a swipe up raises
            // the fader as a scroll wheel rolled up does.
            const dy = wheel.inverted ? -wheel.angleDelta.y : wheel.angleDelta.y
            const notches = dy / 120
            if (notches !== 0)
                fader.ask(fader.position + notches * fader.step)
        }
    }
}
