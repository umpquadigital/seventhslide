import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The heading over a GroupList ("Organization", "Library tabs"), with room
// above to set it off from the group before, and an optional quiet note or
// tools on the right.
Item {
    id: heading
    property string text: ""
    /// A quieter word after the heading ("Front", "Back").
    property string note: ""
    /// A badge after the heading (TODAY), in StatusBadge's blue.
    property string badge: ""
    /// A glyph before the heading (an Icons URL).
    property string iconSource: ""
    property alias tools: toolRow.data
    /// Space above; the first heading under a page header uses less.
    property real topGap: 18

    width: parent ? parent.width : implicitWidth
    implicitHeight: heading.topGap + Math.max(label.implicitHeight, toolRow.implicitHeight) + 7

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 7
        spacing: 8

        Image {
            visible: heading.iconSource.length > 0
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            source: heading.iconSource
            sourceSize: Qt.size(12, 12)
        }
        Text {
            id: label
            visible: heading.text.length > 0
            text: heading.text
            color: Style.textSoft
            font.pixelSize: Style.fontBody
            font.bold: true
        }
        StatusBadge {
            visible: heading.badge.length > 0
            tone: "blue"
            text: heading.badge
        }
        Text {
            visible: heading.note.length > 0
            text: heading.note
            color: Style.textHint
            font.pixelSize: Style.fontSmall
        }
        Item { Layout.fillWidth: true }
        Row {
            id: toolRow
            spacing: 6
        }
    }
}
