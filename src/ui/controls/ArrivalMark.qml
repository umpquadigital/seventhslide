import QtQuick
import SeventhSlide

// What sync knows of a library file that is not here yet
// (``MediaController::arrival``): a ring that fills as it downloads, empty
// while it waits, red when no computer connected has it, and the words beside
// it. Nothing while the file is here.
Row {
    id: mark

    /// ``session.media.arrival(...)``: ``{state, progress, text}``, or empty.
    property var arrival: ({})
    /// The words beside the ring; without them, only the ring.
    property bool showText: true
    /// Widest the mark may be: the words are cut short to fit.
    property real maxWidth: 1e6
    property color ink: Style.textMuted

    readonly property string arrivalState: mark.arrival && mark.arrival.state ? String(mark.arrival.state) : ""
    readonly property bool missing: mark.arrivalState === "missing"

    visible: mark.arrivalState.length > 0
    spacing: 4

    ProgressRing {
        id: ring
        anchors.verticalCenter: parent.verticalCenter
        value: mark.arrival && mark.arrival.progress > 0 ? mark.arrival.progress : 0
        track: mark.missing ? Style.textDanger : Style.controlEdge
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        visible: mark.showText
        width: Math.min(implicitWidth, Math.max(0, mark.maxWidth - ring.width - mark.spacing))
        text: mark.arrival && mark.arrival.text ? String(mark.arrival.text) : ""
        // It may hold another computer's name.
        textFormat: Text.PlainText
        color: mark.missing ? Style.textDanger : mark.ink
        font.pixelSize: Style.fontCaption
        elide: Text.ElideRight
    }
}
