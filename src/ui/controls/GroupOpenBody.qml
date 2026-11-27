import QtQuick
import SeventhSlide

// The body of a GroupRow opened in place: a quieter panel under the row with
// its form rows (whatever is declared inside, in a Column, spaced so each
// steps Style.groupRowH as the rows of the list do), and an optional
// foot ruled off above it, right-aligned - where Delete … goes.
Rectangle {
    id: body
    default property alias content: column.data
    /// What sits at the foot (a Delete … button); none when empty.
    property alias foot: footRow.data

    width: parent ? parent.width : implicitWidth
    implicitHeight: column.y + column.implicitHeight
                    + (footArea.visible ? 16 + footArea.height : 0) + 12
    color: Style.groupOpenBg

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Style.groupRule
    }
    Column {
        id: column
        x: 14
        y: 12
        width: body.width - 26
        spacing: 12
    }
    Item {
        id: footArea
        visible: footRow.children.length > 0
        x: 14
        y: column.y + column.implicitHeight + 16
        width: body.width - 26
        height: footRow.implicitHeight + 12
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 1
            color: Style.groupRule
        }
        Row {
            id: footRow
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 8
        }
    }
}
