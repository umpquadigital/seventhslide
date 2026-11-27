import QtQuick
import SeventhSlide

// What an empty list says in its place: a title (nothing there, or nothing
// matches), and below it what to do. The caller places it and says when it
// shows.
Column {
    id: note
    property string title: ""
    property string hint: ""
    topPadding: 40
    spacing: 6

    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: note.title
        color: Style.text
        font.pixelSize: Style.fontUi
        font.bold: true
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        visible: note.hint.length > 0
        text: note.hint
        color: Style.textCaption
        font.pixelSize: Style.fontBody
    }
}
