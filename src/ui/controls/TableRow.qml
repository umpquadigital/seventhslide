import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A row of a table in a well (Settings > Bibles, Outputs, Stacks): its cells,
// declared inside, laid left to right (a TableGroove between each), padding
// their own content. It is filled and rounded as a pick row is (the Libraries
// & Profiles wells), lighter under the pointer and ``selected``, its left edge
// brightening. A ``live`` row is the service list's blue, as the item on
// screen is there (Services, the open service). A press anywhere on it takes the focus, so a cell open for
// editing elsewhere keeps its edit; ``doubleClicked`` says where, in the row.
Rectangle {
    id: row
    default property alias cells: cellRow.data
    property bool selected: false
    property bool live: false
    readonly property bool hovered: area.containsMouse
    signal clicked()
    signal doubleClicked(real x)

    width: row.ListView.view
           ? row.ListView.view.width - row.ListView.view.leftMargin - row.ListView.view.rightMargin
           : (parent ? parent.width : implicitWidth)
    implicitHeight: Style.listRowH
    radius: Style.radiusSmall
    color: row.live ? (row.hovered ? Style.liveRowHover : Style.liveRow)
                    : Style.panelRowBg(row.selected, row.hovered)

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        radius: 1
        color: row.live ? Style.accent : Style.listAccent(row.selected, row.hovered)
    }

    // Under the cells: the click, and the focus.
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        onPressed: area.forceActiveFocus()
        onClicked: row.clicked()
        onDoubleClicked: function (mouse) { row.doubleClicked(mouse.x) }
    }

    RowLayout {
        id: cellRow
        anchors.fill: parent
        spacing: 0
    }
}
