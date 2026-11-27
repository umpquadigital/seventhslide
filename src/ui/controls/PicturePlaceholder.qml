import QtQuick
import SeventhSlide

// In an editor, where a library picture not here would be: a box that says
// so, with how far it has come when it is on its way, so the slide does not
// look empty and the picture is not taken for gone. The outputs draw nothing
// in its place.
Rectangle {
    id: holder

    property var session: null
    property string filename: ""
    /// Whether the picture belongs here at all.
    property bool wanted: true

    readonly property bool here: !holder.session || holder.filename.length === 0
        || holder.session.media.mediaFileUrl(MediaKind.Image, holder.filename,
                                             holder.session.media.filesRevision).length > 0

    visible: holder.wanted && !holder.here
    color: Style.thumbWell
    border.width: 1
    border.color: Style.controlEdge

    ArrivalMark {
        id: mark
        anchors.centerIn: parent
        maxWidth: holder.width - 8
        arrival: holder.visible
                 ? holder.session.media.arrival(MediaKind.Image, holder.filename,
                                                holder.session.media.arrivalsRevision)
                 : ({})
    }
    Text {
        anchors.centerIn: parent
        width: Math.min(implicitWidth, holder.width - 8)
        visible: !mark.visible
        text: qsTr("Picture missing")
        color: Style.textMuted
        font.pixelSize: Style.fontCaption
        elide: Text.ElideRight
    }
}
