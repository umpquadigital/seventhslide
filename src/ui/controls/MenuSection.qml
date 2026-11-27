import QtQuick
import SeventhSlide

// A group in an AppMenu: a MenuHeading over the items declared inside. One
// item, so a Repeater can make a heading and its items together (one section
// per group a model lists); as wide as its widest item asks to be, as the
// menu's own items are.
Item {
    id: section

    property alias title: heading.text
    default property alias items: column.data

    width: parent ? parent.width : implicitWidth
    implicitWidth: {
        let w = 0
        for (const child of column.children)
            w = Math.max(w, child.implicitWidth)
        return w
    }
    implicitHeight: column.implicitHeight

    Column {
        id: column
        width: parent.width

        MenuHeading {
            id: heading
        }
    }
}
