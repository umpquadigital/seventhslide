pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// The shape palette on the Insert tab: one outlined grid of glyph cells, two
// rows high. ``cells`` are [{id, icon, hint, on}]; ``clicked(id)``
// says which was pressed. A drawing tool that is running is ``on``.
Rectangle {
    id: palette
    property var cells: []
    property int columns: 7
    signal clicked(string id)

    readonly property int cellSize: Style.ribbonH + 2
    readonly property int glyph: Style.ribbonIcon + 2

    implicitWidth: palette.columns * palette.cellSize + 2
    implicitHeight: Style.ribbonTallH
    radius: Style.radiusSmall
    color: Style.fieldFace
    border.color: Style.controlEdge
    clip: true

    Grid {
        x: 1
        y: 1
        columns: palette.columns
        Repeater {
            model: palette.cells
            delegate: Rectangle {
                id: cell
                required property var modelData
                readonly property bool on: !!cell.modelData.on
                width: palette.cellSize
                height: (palette.height - 2) / 2
                color: cell.on ? Style.onTint : (hover.containsMouse ? Style.btnGhostHover : "transparent")
                Image {
                    anchors.centerIn: parent
                    width: palette.glyph
                    height: palette.glyph
                    source: Icons.url(String(cell.modelData.icon), cell.on ? Style.onInk : Style.text, 2)
                    sourceSize: Qt.size(palette.glyph, palette.glyph)
                }
                MouseArea {
                    id: hover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: palette.clicked(String(cell.modelData.id))
                }
            }
        }
    }
}
