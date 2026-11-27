import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One column of a ListWell: an optional header strip with a title (sentence
// case, near the left edge) and tools on the right, and under it the content
// (whatever is declared inside) in a ColumnLayout, sized with Layout.* as in
// any column. ``ruleLeft`` draws the line between it and the pane before.
//
// headerHeight: 30 over a column of a browser, Style.wellBarH (40) over a
// song editor well. A header that starts with a control instead of a title
// sets ``title: ""`` and puts it in ``leading``.
Item {
    id: pane
    property string title: ""
    /// A glyph before the title (an Icons URL); none when empty.
    property string iconSource: ""
    property real headerHeight: 30
    property bool showHeader: true
    /// Title ink: Style.textCaption while another column has the focus.
    property color titleInk: Style.textSoft
    property bool ruleLeft: false
    /// Controls at the header's left, instead of a title.
    property alias leading: leadingRow.data
    /// Controls at the header's right.
    property alias tools: toolRow.data
    default property alias content: body.data

    implicitWidth: column.implicitWidth + (pane.ruleLeft ? 1 : 0)
    implicitHeight: column.implicitHeight

    Rectangle {
        visible: pane.ruleLeft
        width: 1
        height: parent.height
        color: Style.groupRule
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.leftMargin: pane.ruleLeft ? 1 : 0
        spacing: 0

        Rectangle {
            visible: pane.showHeader
            Layout.fillWidth: true
            Layout.preferredHeight: pane.headerHeight
            color: Style.wellHead

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Style.groupRule
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: pane.title.length ? 10 : 8
                anchors.rightMargin: 8
                spacing: 6

                Row {
                    id: leadingRow
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6
                    visible: children.length > 0
                }
                Image {
                    visible: pane.iconSource.length > 0 && pane.title.length > 0
                    Layout.preferredWidth: 14
                    Layout.preferredHeight: 14
                    Layout.alignment: Qt.AlignVCenter
                    source: pane.iconSource
                    sourceSize: Qt.size(14, 14)
                }
                Text {
                    visible: pane.title.length > 0
                    Layout.fillWidth: true
                    text: pane.title
                    color: pane.titleInk
                    font.pixelSize: Style.fontBody
                    font.bold: true
                    elide: Text.ElideRight
                }
                Item {
                    visible: pane.title.length === 0
                    Layout.fillWidth: true
                }
                Row {
                    id: toolRow
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 6
                }
            }
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0
        }
    }
}
