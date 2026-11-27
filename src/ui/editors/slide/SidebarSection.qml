import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One section of the sidebar: a well whose header folds it. ``rows`` are
// SidebarRows (or anything as wide as the section), stacked in a Column,
// which tells each row whether it is the first one shown.
Rectangle {
    id: section
    required property SlideEditState dialog
    required property string sectionId
    property string title: ""
    default property alias rows: body.data
    readonly property bool open: !section.dialog.sectionClosed[section.sectionId]

    Layout.fillWidth: true
    implicitHeight: head.height + (section.open ? body.implicitHeight : 0) + 2
    radius: Style.radiusLarge
    color: Style.wellBg
    border.color: Style.wellEdge
    clip: true

    Rectangle {
        id: head
        x: 1
        y: 1
        width: parent.width - 2
        height: 26
        color: Style.wellHead
        Rectangle {
            visible: section.open
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: Style.groupRule
        }
        Text {
            x: 10
            anchors.verticalCenter: parent.verticalCenter
            text: section.title
            color: Style.textSoft
            font.pixelSize: Style.fontBody
            font.bold: true
        }
        Image {
            anchors.right: parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: 10
            height: 10
            rotation: section.open ? 0 : -90
            source: Icons.url("chevronDown", Style.textCaption, 2)
            sourceSize: Qt.size(10, 10)
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                const next = Object.assign({}, section.dialog.sectionClosed)
                next[section.sectionId] = section.open
                section.dialog.sectionClosed = next
            }
        }
    }
    Column {
        id: body
        visible: section.open
        x: 1
        y: head.y + head.height
        width: parent.width - 2
    }
}
