pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// A row of the controller options menu that clears something.
Rectangle {
    id: dangerRow
    property string label: ""
    signal activated()

    width: parent ? parent.width : implicitWidth
    implicitWidth: 12 + labelText.implicitWidth + 12
    height: 30
    color: dangerHover.containsMouse ? Style.btnDangerHover : "transparent"

    Text {
        id: labelText
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        text: dangerRow.label
        color: dangerHover.containsMouse ? Style.btnDangerInkHover
                                         : Style.btnDangerInk
        font.pixelSize: Style.fontBody
        elide: Text.ElideRight
    }

    MouseArea {
        id: dangerHover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: dangerRow.activated()
    }
}
