import QtQuick
import SeventhSlide

// One row of a sidebar section: a label at the left, the controls (declared
// inside) at the right, and a hairline above unless it is the first row the
// section shows.
Rectangle {
    id: row
    required property string label
    default property alias control: slot.data
    /// The controls take the row from its left edge (a strip of choices), no label.
    property bool wide: false

    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(Style.sidebarRowH, slot.implicitHeight + 8)
    color: Style.groupBg

    Rectangle {
        visible: !row.Positioner.isFirstItem
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Style.groupRule
    }
    Text {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.right: slot.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        text: row.label
        color: Style.text
        font.pixelSize: Style.fontBody
        elide: Text.ElideRight
    }
    Row {
        id: slot
        x: row.wide ? 10 : row.width - width - 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
    }
}
