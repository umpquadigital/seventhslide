import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One group on the ribbon: its controls in two rows, a hairline before it.
// No heading: a control's icon, label or tooltip says what it is. ``row1``
// and ``row2`` take the controls; a group with one row leaves ``row2`` empty.
Item {
    id: group
    /// Controls on the top row, and the bottom row.
    property alias row1: top.data
    property alias row2: bottom.data
    /// A control as tall as both rows (a gallery), before them.
    property alias tall: tallSlot.data
    /// The first group has no hairline.
    property bool first: false
    /// Fills the room left on the ribbon (the last group).
    property bool grow: false

    implicitWidth: tallSlot.implicitWidth + (tallSlot.children.length > 0 ? 8 : 0)
                   + column.implicitWidth + 2 * Style.ribbonGroupPad + (group.first ? 0 : 1)
    implicitHeight: column.implicitHeight

    Rectangle {
        visible: !group.first
        width: 1
        height: parent.height
        color: Style.border
    }
    Row {
        id: tallSlot
        x: (group.first ? 0 : 1) + Style.ribbonGroupPad
        y: 0
        spacing: 6
        height: Style.ribbonTallH
    }
    ColumnLayout {
        id: column
        x: (group.first ? 0 : 1) + Style.ribbonGroupPad + tallSlot.implicitWidth + (tallSlot.children.length > 0 ? 8 : 0)
        y: 0
        height: parent.height
        spacing: Style.ribbonRowGap
        Row {
            id: top
            spacing: Style.ribbonSpacing
            Layout.preferredHeight: Style.ribbonH
            Layout.alignment: Qt.AlignLeft | Qt.AlignTop
        }
        Row {
            id: bottom
            spacing: Style.ribbonSpacing
            visible: children.length > 0
            Layout.preferredHeight: Style.ribbonH
            Layout.alignment: Qt.AlignLeft | Qt.AlignTop
        }
    }
}
