import QtQuick
import SeventhSlide

// A separator between groups of items in an AppMenu.
Item {
    width: parent ? parent.width : 0
    implicitHeight: 9
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        height: 1
        color: Style.menuRule
    }
}
