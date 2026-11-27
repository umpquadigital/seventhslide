import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The bar over a table in a well: its columns' names (TableColumnHead, a
// TableGroove between), at the height and in the shade of the Libraries &
// Profiles wells' bars. It runs edge to edge, so each name sits as far into
// its column as the next, the first from the well's edge. The rows below are
// inset by the list's margin (6), so the last column is that much wider here;
// ``rightInset`` keeps clear of the list's scroll bar.
Rectangle {
    id: bar
    default property alias columns: row.data
    property real rightInset: 0

    Layout.fillWidth: true
    Layout.preferredHeight: 30
    implicitHeight: 30
    color: Style.wellHead

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Style.groupRule
    }
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.rightMargin: bar.rightInset
        anchors.bottomMargin: 1
        spacing: 0
    }
}
