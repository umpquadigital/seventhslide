pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// Choices that belong together (align left / centre / right, B I U) as one
// outlined strip, SegmentedControl's shape with icons or glyph labels, where
// any number may be on. ``items``: [{id, icon, label, bold, italic,
// underline, strike, on, hint}]; ``clicked(id)`` says which was pressed.
Rectangle {
    id: strip
    property var items: []
    /// In the sidebar: the shorter control height.
    property bool compact: false
    /// Its height, and each choice's width: the ribbon's, the sidebar's, or
    /// a field's beside it (the song editor's B / I / U, Style.controlH).
    property int cellH: strip.compact ? Style.btnCompactH : Style.ribbonH
    signal clicked(string id)

    readonly property int glyph: strip.compact ? Style.btnStandardIcon : Style.ribbonIcon

    implicitWidth: row.implicitWidth + 2
    implicitHeight: strip.cellH
    radius: Style.radiusSmall
    color: "transparent"
    border.color: Style.btnEdge
    clip: true
    gradient: Gradient {
        GradientStop { position: 0.0; color: Style.btnNeutralTop }
        GradientStop { position: 1.0; color: Style.btnNeutralBot }
    }

    Row {
        id: row
        x: 1
        y: 1
        Repeater {
            model: strip.items
            delegate: Rectangle {
                id: cell
                required property var modelData
                required property int index
                readonly property bool on: !!cell.modelData.on
                width: strip.cellH
                height: strip.cellH - 2
                color: cell.on ? Style.onTint : (hover.containsMouse ? Style.btnNeutralHoverTop : "transparent")
                Rectangle {
                    visible: cell.index > 0
                    width: 1
                    height: parent.height
                    color: Style.btnEdge
                }
                Image {
                    visible: !!cell.modelData.icon
                    anchors.centerIn: parent
                    width: strip.glyph
                    height: strip.glyph
                    source: cell.modelData.icon
                            ? Icons.url(cell.modelData.icon, cell.on ? Style.onInk : Style.text, 2) : ""
                    sourceSize: Qt.size(strip.glyph, strip.glyph)
                }
                BadgeText {
                    visible: !cell.modelData.icon
                    anchors.centerIn: parent
                    text: cell.modelData.label || ""
                    color: cell.on ? Style.onInk : Style.text
                    font.pixelSize: strip.compact ? Style.fontBody : Style.ribbonFont
                    font.bold: !!cell.modelData.bold
                    font.italic: !!cell.modelData.italic
                    font.underline: !!cell.modelData.underline
                    font.strikeout: !!cell.modelData.strike
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: strip.clicked(String(cell.modelData.id))
                }
            }
        }
    }
}
