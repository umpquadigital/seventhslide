import QtQuick
import SeventhSlide

// The bar down a list row's left edge: the row's selection and hover in the
// accent's shades, rounded with the row's left corners (``cornerRadius``:
// the row's radius).
Rectangle {
    property bool selected: false
    property bool hovered: false
    property real cornerRadius: 0

    anchors.left: parent.left
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: 3
    topLeftRadius: cornerRadius
    bottomLeftRadius: cornerRadius
    color: Style.listAccent(selected, hovered)
}
