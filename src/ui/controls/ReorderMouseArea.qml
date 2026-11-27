import QtQuick
import SeventhSlide

// The gesture on one row of a ReorderDrag list: press, move past a few pixels
// to pick the row up, release to drop it. A press and release without moving
// is a click and moves nothing.
MouseArea {
    id: area

    required property ReorderDrag reorder
    /// The item the rows are laid out in; slots are read in its coordinates.
    required property Item rows
    required property int index
    /// What the ghost shows.
    property string label: ""
    property string icon: ""

    /// The row was picked up.
    signal dragStarted()

    readonly property bool dragging: area._dragging
    property bool _dragging: false
    property real _pressX: 0
    property real _pressY: 0
    property real _pressRowsY: 0

    hoverEnabled: true
    preventStealing: true
    cursorShape: area._dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor

    onPressed: function (mouse) {
        area._dragging = false
        area._pressX = mouse.x
        area._pressY = mouse.y
        area._pressRowsY = area.mapToItem(area.rows, mouse.x, mouse.y).y
        area.reorder.press(area.index)
    }
    onPositionChanged: function (mouse) {
        if (!area.pressed)
            return
        const y = area.mapToItem(area.rows, mouse.x, mouse.y).y
        if (!area._dragging && Math.abs(y - area._pressRowsY) > 5) {
            area._dragging = true
            area.dragStarted()
            area.reorder.start(area.label, area.icon, area.index + 1, area._pressX, area._pressY)
        }
        if (area._dragging)
            area.reorder.follow(y, area, mouse.x, mouse.y)
    }
    onReleased: {
        if (area._dragging)
            area.reorder.finish()
        else
            area.reorder.cancel()
        area._dragging = false
    }
    onCanceled: {
        area.reorder.cancel()
        area._dragging = false
    }
}
