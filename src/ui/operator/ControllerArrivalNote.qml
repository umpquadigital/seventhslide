pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// Under the controller's heading, while what is live needs files that are not
// here yet: which, and how far each has come (``session.liveArrivals``). It
// stops nothing: what is here shows, and the rest shows once it has come.
// While sync is paused for what is live, the files are paused too, and it
// says so.
Rectangle {
    id: note

    property var session: null

    readonly property var arrivals: note.session ? note.session.liveArrivals : []
    readonly property bool paused: !!note.session && note.session.media.syncPaused
    /// Some of them will come: not all are missing on every computer connected.
    readonly property bool coming: note.arrivals.some(function (a) { return a.state !== "missing" })

    visible: note.arrivals.length > 0
    implicitHeight: visible ? lines.implicitHeight + 12 : 0
    color: Style.panelRow
    border.width: 1
    border.color: Style.controlEdge
    radius: 4

    Column {
        id: lines
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        spacing: 4

        Text {
            width: parent.width
            text: {
                const n = note.arrivals.length
                const what = n === 1 ? qsTr("Live uses a file that is not on this computer.")
                                     : qsTr("Live uses %1 files that are not on this computer.").arg(n)
                if (!note.coming)
                    return what
                return what + " " + (n === 1 ? qsTr("It shows once it has come.")
                                             : qsTr("What is on its way shows once it has come."))
            }
            color: Style.textWarning
            font.pixelSize: Style.fontBody
            wrapMode: Text.Wrap
        }

        Repeater {
            model: note.arrivals
            delegate: Row {
                id: entry
                required property var modelData
                /// Half the note each: the name, then how far it has come.
                readonly property real half: Math.max(0, note.width - 12) / 2
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, entry.half)
                    text: String(entry.modelData.file || "")
                    textFormat: Text.PlainText
                    color: Style.text
                    font.pixelSize: Style.fontCaption
                    elide: Text.ElideMiddle
                }
                ArrivalMark {
                    anchors.verticalCenter: parent.verticalCenter
                    maxWidth: entry.half
                    arrival: entry.modelData
                }
            }
        }

        Text {
            width: parent.width
            visible: note.paused && note.coming
            text: qsTr("Sync is paused while something is live, so they come once nothing is. "
                       + "Turn off Pause during services in Settings to let them come now.")
            color: Style.textMuted
            font.pixelSize: Style.fontCaption
            wrapMode: Text.Wrap
        }
    }
}
