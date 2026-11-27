import QtQuick
import SeventhSlide

// The bar along the top of each docked panel, the same height and face on
// every one, ruled off from what is under it. What it shows is declared
// inside.
Rectangle {
    id: bar
    /// Under the pointer, where pressing it does something.
    property bool hot: false

    height: Style.dockHeaderH
    color: bar.hot ? Style.dockHeaderHover : Style.dockHeaderBg

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Style.border
    }
}
