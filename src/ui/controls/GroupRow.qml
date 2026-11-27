import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One row of a GroupList: whatever is declared inside, laid out left to right
// (a tile, a title, a control...), 40 high or taller, with a hairline above
// unless it is the first row. A row wrapped with its GroupOpenBody in a
// Column binds ``ruleAbove`` to the wrapper's place in the list. ``interactive`` rows light up under the pointer
// and emit ``clicked``; ``selected`` marks the row that is open or chosen.
// In a reorderable list, ``dropAbove`` / ``dropBelow`` draw the line where a
// dragged row would land, and ``lifted`` fades the row being dragged.
Rectangle {
    id: row
    default property alias content: layout.data
    property bool interactive: false
    property bool selected: false
    property bool dropAbove: false
    property bool dropBelow: false
    property bool lifted: false
    property bool ruleAbove: !row.Positioner.isFirstItem
    readonly property bool hovered: area.containsMouse
    signal clicked()
    signal doubleClicked()

    width: parent ? parent.width : implicitWidth
    implicitWidth: layout.implicitWidth + 22
    implicitHeight: Math.max(Style.groupRowH, layout.implicitHeight + 12)
    color: row.selected ? Style.groupSel
         : (row.interactive && area.containsMouse ? Style.groupHover : "transparent")
    opacity: row.lifted ? 0.4 : 1
    z: row.dropAbove || row.dropBelow ? 1 : 0

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Style.groupRule
        visible: row.ruleAbove
    }
    Rectangle {
        visible: row.dropAbove || row.dropBelow
        anchors.left: parent.left
        anchors.right: parent.right
        y: row.dropAbove ? -1 : row.height - 1
        height: 2
        color: Style.focusEdge
    }
    MouseArea {
        id: area
        anchors.fill: parent
        enabled: row.interactive
        hoverEnabled: row.interactive
        cursorShape: row.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: row.clicked()
        onDoubleClicked: row.doubleClicked()
    }
    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        anchors.topMargin: 6
        anchors.bottomMargin: 6
        spacing: 10
    }
}
