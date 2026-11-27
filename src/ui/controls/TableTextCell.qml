import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A table cell of words, padded (``leftInset`` at its start), or while
// ``open`` a field over them with its text where theirs was: Enter or
// clicking away keeps it (``committed``), Escape drops it (``canceled``). The
// table opens it, on a double click; one born open (a row for a new author,
// whose name is wanted first) takes the keyboard at once. ``escapeKeeps``
// makes Escape keep the text as Enter does, the song editor's rule for every
// field. ``dim`` fades the words (a Bible that is off); ``centered`` sets
// them in the middle of the cell (a row's place in an Order column).
Item {
    id: cell
    property string text: ""
    property bool open: false
    property bool escapeKeeps: false
    property bool dim: false
    property bool bold: false
    property bool centered: false
    property color ink: Style.text
    property int leftInset: 10
    property int maxLength: 32767
    signal committed(string text)
    signal canceled()

    Layout.fillHeight: true

    // Fires for a cell born open too: its binding is written at creation.
    onOpenChanged: {
        if (!cell.open)
            return
        input.text = cell.text
        input.selectAll()
        input.forceActiveFocus()
    }

    Text {
        visible: !cell.open
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: cell.leftInset
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        horizontalAlignment: cell.centered ? Text.AlignHCenter : Text.AlignLeft
        text: cell.text
        color: cell.ink
        opacity: cell.dim ? Style.offOpacity : 1
        font.pixelSize: Style.fontBody
        font.weight: cell.bold ? Font.DemiBold : Font.Normal
        elide: Text.ElideRight
    }
    Rectangle {
        visible: cell.open
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: cell.leftInset - 6
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        height: 24
        radius: Style.radiusSmall
        color: Style.fieldFaceFocus
        border.color: Style.focusEdge

        TextInput {
            id: input
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            verticalAlignment: TextInput.AlignVCenter
            clip: true
            color: Style.text
            font.pixelSize: Style.fontBody
            selectByMouse: true
            selectionColor: Style.accent
            maximumLength: cell.maxLength
            Keys.onReturnPressed: function (event) {
                event.accepted = true
                cell.committed(input.text)
            }
            Keys.onEnterPressed: function (event) {
                event.accepted = true
                cell.committed(input.text)
            }
            Keys.onEscapePressed: function (event) {
                event.accepted = true
                if (cell.escapeKeeps)
                    cell.committed(input.text)
                else
                    cell.canceled()
            }
            onActiveFocusChanged: {
                if (!input.activeFocus && cell.open)
                    cell.committed(input.text)
            }
        }
    }
}
