import QtQuick
import SeventhSlide

// A dialog's search box: the one field face with a magnifier, a placeholder
// that shows while it is empty and unfocused, and the field edges for hover
// and focus. Callers bind ``text`` and write it back from ``onTextChanged``;
// ``escaped`` fires on Escape, which the field takes. (The library panel's search, with its glyph
// and clear button, is its own.)
Rectangle {
    id: field
    property alias text: input.text
    property string placeholder: qsTr("Search…")
    readonly property alias inputFocused: input.activeFocus
    signal escaped()

    /// Leave the field: drop the selection and the focus.
    function blur() {
        input.deselect()
        input.focus = false
    }

    implicitHeight: Style.controlH
    radius: Style.radiusSmall
    color: input.activeFocus ? Style.fieldFaceFocus : Style.fieldFace
    border.width: 1
    border.color: input.activeFocus ? Style.focusEdge
                : (hover.containsMouse ? Style.controlEdgeHover : Style.controlEdge)

    Image {
        id: glyph
        anchors.left: parent.left
        anchors.leftMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        width: 12
        height: 12
        source: Icons.url("search", Style.fieldPlaceholder, 2)
        sourceSize: Qt.size(12, 12)
    }
    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: glyph.width + 16
        anchors.rightMargin: 9
        verticalAlignment: TextInput.AlignVCenter
        color: Style.text
        font.pixelSize: Style.fontBody
        selectByMouse: true
        clip: true
        Keys.onEscapePressed: function (event) {
            field.escaped()
            event.accepted = true
        }
    }
    Text {
        anchors.fill: parent
        anchors.leftMargin: input.anchors.leftMargin
        anchors.rightMargin: 9
        verticalAlignment: Text.AlignVCenter
        text: field.placeholder
        color: Style.fieldPlaceholder
        font.pixelSize: Style.fontBody
        visible: !input.text.length && !input.activeFocus
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        cursorShape: Qt.IBeamCursor
    }
}
