import QtQuick
import SeventhSlide

// A grouped list in a window: rows (GroupRow, or a GroupRow followed by its
// GroupOpenBody) stacked in one rounded block with a hairline between them.
// The rows are whatever is declared inside, laid out in a Column.
Rectangle {
    id: group
    default property alias rows: column.data
    /// Spacing is fixed: rows draw their own dividers.
    readonly property alias column: column

    implicitWidth: column.implicitWidth + 2
    implicitHeight: column.implicitHeight + 2
    radius: Style.radiusLarge
    color: Style.groupBg
    border.color: Style.groupEdge
    clip: true

    Column {
        id: column
        x: 1
        y: 1
        width: group.width - 2
    }
}
