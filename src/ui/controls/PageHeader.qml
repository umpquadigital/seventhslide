import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The top of a window page: the title, one or two plain sentences under it,
// and at most one button (or a search and a button, on a gallery page) to the
// title's right. The title sits at the same height with or without tools.
ColumnLayout {
    id: header
    property string title: ""
    property string text: ""
    /// The page's button(s), right of the title.
    property alias tools: toolRow.data

    spacing: 5

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: Style.controlH
        spacing: 14

        Text {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: header.title
            color: Style.textStrong
            font.pixelSize: Style.fontHeading
            font.bold: true
            elide: Text.ElideRight
        }
        Row {
            id: toolRow
            Layout.alignment: Qt.AlignVCenter
            spacing: 8
        }
    }
    Text {
        visible: header.text.length > 0
        Layout.fillWidth: true
        Layout.maximumWidth: 600
        text: header.text
        color: Style.textCaption
        font.pixelSize: Style.fontBody
        lineHeightMode: Text.FixedHeight
        lineHeight: 18
        wrapMode: Text.WordWrap
    }
}
