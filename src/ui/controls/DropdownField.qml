import QtQuick
import SeventhSlide

// A field that opens a dropdown: the current choice and a chevron, in the
// field face. The caller owns the menu: it opens and closes it on
// ``clicked`` and says whether it is showing with ``open``, which edges the
// field in focus blue and turns the chevron. Switched off, it fades and
// ignores the pointer. With ``placeholder`` the text is a stand-in ("Nothing",
// "Select service…") and drawn as one. A ``detail`` is said quietly at the
// right, as a menu item's shortcut is (a size beside its name).
Rectangle {
    id: field

    /// The current choice, as the field shows it.
    property string text: ""
    /// A glyph from the table before the text; none when empty.
    property string icon: ""
    /// More about the choice, quiet, before the chevron; none when empty.
    property string detail: ""
    /// ``text`` stands in for a choice not made yet.
    property bool placeholder: false
    /// The choice's size (the ribbon's is larger).
    property int fontSize: Style.fontBody
    /// Its menu is showing.
    property bool open: false
    signal pressed()
    signal clicked()

    implicitWidth: Style.fieldLongW
    implicitHeight: Style.controlH
    radius: Style.radiusSmall
    color: Style.fieldFace
    border.color: field.open ? Style.focusEdge
                : (hover.containsMouse ? Style.controlEdgeHover : Style.controlEdge)
    opacity: field.enabled ? 1 : Style.offOpacity

    Image {
        id: glyph
        visible: field.icon.length > 0
        x: 9
        anchors.verticalCenter: parent.verticalCenter
        width: visible ? 12 : 0
        height: 12
        source: visible ? Icons.url(field.icon, Style.text, 2) : ""
        sourceSize: Qt.size(12, 12)
    }
    Text {
        anchors.left: glyph.visible ? glyph.right : parent.left
        anchors.right: detailText.visible ? detailText.left : chevron.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: glyph.visible ? 6 : 9
        anchors.rightMargin: detailText.visible ? 8 : 6
        text: field.text
        color: field.placeholder ? Style.fieldPlaceholder : Style.text
        font.pixelSize: field.fontSize
        elide: Text.ElideRight
    }
    Text {
        id: detailText
        visible: field.detail.length > 0
        anchors.right: chevron.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: field.detail
        color: Style.textFaint
        font.pixelSize: Style.fontSmall
    }
    Image {
        id: chevron
        anchors.right: parent.right
        anchors.rightMargin: 9
        anchors.verticalCenter: parent.verticalCenter
        width: 10
        height: 10
        rotation: field.open ? 180 : 0
        source: Icons.url("chevronDown", Style.chevronInk, 2.5)
        sourceSize: Qt.size(10, 10)
    }
    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPressed: field.pressed()
        onClicked: field.clicked()
    }
}
