import QtQuick
import SeventhSlide

// The square tile at the start of a reorderable row: a Library tab's glyph, a
// build step's number. It is the row's drag handle - the caller puts its
// ReorderMouseArea inside - and it lights up under the pointer so it reads as
// one. Without ``draggable`` it is just the tile (an output's icon).
Rectangle {
    id: tile
    /// A glyph URL to draw, or none.
    property string iconSource: ""
    /// A number or short label to draw instead.
    property string text: ""
    property bool draggable: false
    readonly property bool hovered: hover.hovered

    implicitWidth: Style.tileSize
    implicitHeight: Style.tileSize
    radius: Style.radiusSmall
    color: tile.draggable && hover.hovered ? Style.tileHoverBg : Style.tileBg
    border.color: tile.draggable && hover.hovered ? Style.tileHoverEdge : Style.tileEdge

    HoverHandler {
        id: hover
        enabled: tile.draggable
        cursorShape: Qt.OpenHandCursor
    }
    Image {
        visible: tile.iconSource.length > 0
        anchors.centerIn: parent
        width: 14
        height: 14
        source: tile.iconSource
        sourceSize: Qt.size(14, 14)
    }
    BadgeText {
        visible: tile.text.length > 0
        anchors.centerIn: parent
        text: tile.text
        color: Style.textSoft
        font.pixelSize: Style.fontBody
        font.weight: Font.DemiBold
    }
}
