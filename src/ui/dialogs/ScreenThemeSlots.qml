pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// One output's themes in a theme map: its name, then its slots, a row per
// category with two side by side (the song editor's Themes tab and a
// service's settings). A map with slots in more than one category heads each
// row with it.
Rectangle {
    id: block
    required property ThemeMapEditor map
    required property string outputId
    required property string outputName
    readonly property bool headed: block.map.slotGroups.length > 1

    height: column.implicitHeight + 24
    radius: Style.radiusLarge
    color: Style.groupBg
    border.color: Style.groupEdge

    Column {
        id: column
        x: 12
        y: 12
        width: parent.width - 24
        spacing: 10

        Text {
            width: parent.width
            text: block.outputName
            color: Style.textBright
            font.pixelSize: Style.fontBody
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Repeater {
            model: block.map.slotGroups
            delegate: Column {
                id: group
                required property var modelData
                width: column.width
                spacing: 4

                Text {
                    visible: block.headed
                    width: parent.width
                    text: String(group.modelData.label || "")
                    color: Style.textCaption
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
                Row {
                    id: slots
                    width: parent.width
                    spacing: 10
                    Repeater {
                        model: group.modelData.slots
                        delegate: ThemeSlot {
                            id: tile
                            required property var modelData
                            width: (slots.width - slots.spacing) / 2
                            map: block.map
                            pick: tile.modelData
                            outputId: block.outputId
                            outputName: block.outputName
                        }
                    }
                }
            }
        }
    }
}
