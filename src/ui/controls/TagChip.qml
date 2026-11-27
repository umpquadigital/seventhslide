import QtQuick
import SeventhSlide

// A tag someone typed - a service tag, a song topic, a theme tag: a small grey
// chip. With ``removable`` it ends in an ×, which emits ``removeClicked``.
// (What the app reports - TODAY, IN USE, OFF - is a StatusBadge instead.)
Rectangle {
    id: chip
    property string text: ""
    property bool removable: false
    signal removeClicked()

    implicitWidth: label.implicitWidth + (chip.removable ? 7 + 17 : 14)
    implicitHeight: 20
    radius: Style.radiusSmall
    color: Style.tagBg
    border.color: Style.tagEdge

    Text {
        id: label
        x: 7
        anchors.verticalCenter: parent.verticalCenter
        text: chip.text
        color: Style.tagInk
        font.pixelSize: Style.fontSmall
    }
    Image {
        visible: chip.removable
        anchors.right: parent.right
        anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        width: 10
        height: 10
        source: Icons.url("close", removeArea.containsMouse ? Style.textStrong : Style.textCaption, 2.5)
        sourceSize: Qt.size(10, 10)
        MouseArea {
            id: removeArea
            anchors.fill: parent
            anchors.margins: -3
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: chip.removeClicked()
        }
    }
}
