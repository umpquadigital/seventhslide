import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One service in the Services window's table: a thumbnail of its first
// slide, its name, its date and its time, the one loaded in the service
// list's blue, in the header's columns (``thumbW``, ``dateW``, ``timeW``,
// padding and all). A service with no date says so, quietly; one with no
// slide to show has a glyph in its thumbnail's place. Its words are
// ServiceBrowser's roles; ``thumbUrl`` is ServiceThumbs'.
TableRow {
    id: row

    required property var serviceId
    required property string name
    required property string dateLabel
    required property string timeLabel
    required property bool isOpen
    /// The thumbnail; "" for none.
    property string thumbUrl: ""
    /// How many items the service holds, said under its name; -1 for
    /// nothing said.
    property int itemCount: -1
    property int thumbW: 64
    property int thumbH: 36
    property int dateW: 132
    property int timeW: 84

    live: row.isOpen
    implicitHeight: row.thumbH + 8

    // The thumbnail, a column of its own, inset as a cell's words are.
    Item {
        Layout.preferredWidth: row.thumbW + 16
        Layout.fillHeight: true

        Rectangle {
            id: thumbBox
            x: 8
            anchors.verticalCenter: parent.verticalCenter
            width: row.thumbW
            height: row.thumbH
            radius: 2
            color: Style.thumbWell
            clip: true

            Image {
                id: thumb
                objectName: "serviceThumb"
                anchors.fill: parent
                visible: status === Image.Ready
                source: row.thumbUrl
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                sourceSize: Qt.size(row.thumbW * 2, row.thumbH * 2)
            }
            // Until the slide is in, and in its place when there is none:
            // as the Library's rows show a file without a picture.
            Image {
                anchors.centerIn: parent
                visible: thumb.status !== Image.Ready
                width: Math.min(18, parent.height - 4)
                height: width
                source: Icons.url("list", Style.textCaption, 2)
                sourceSize: Qt.size(18, 18)
            }
        }
    }
    TableGroove {}
    // The name, and under it how many items the service holds.
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: row.name
                color: Style.text
                font.pixelSize: Style.fontBody
                font.weight: row.selected ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
            Text {
                visible: row.itemCount >= 0
                width: parent.width
                text: row.itemCount === 0 ? qsTr("No items")
                    : row.itemCount === 1 ? qsTr("1 item")
                    : qsTr("%1 items").arg(row.itemCount)
                color: row.live ? Style.textSoft : Style.textCaption
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
            }
        }
    }
    TableGroove {}
    TableTextCell {
        Layout.preferredWidth: row.dateW
        text: row.dateLabel.length ? row.dateLabel : qsTr("No date")
        ink: !row.dateLabel.length ? Style.textCaption : (row.live ? Style.text : Style.textSoft)
    }
    TableGroove {}
    TableTextCell {
        Layout.preferredWidth: row.timeW
        text: row.timeLabel
        ink: row.live ? Style.text : Style.textSoft
    }
}
