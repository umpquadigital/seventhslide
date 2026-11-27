import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A column's name on a TableHeaderBar. A ``sortable`` one brightens under the
// pointer and emits ``clicked``; the column the table is ``sorted`` by shows a
// chevron, up when ``ascending``.
Item {
    id: head
    property string label: ""
    property bool sortable: false
    property bool sorted: false
    property bool ascending: true
    signal clicked()

    readonly property color ink: headArea.containsMouse && head.sortable ? Style.textBright
                                                                         : Style.textSoft

    Layout.fillHeight: true
    implicitWidth: labelRow.implicitWidth + 20

    Row {
        id: labelRow
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: head.label
            color: head.ink
            font.pixelSize: Style.fontBody
            font.bold: true
        }
        Image {
            visible: head.sorted
            anchors.verticalCenter: parent.verticalCenter
            width: 10
            height: 10
            rotation: head.ascending ? 180 : 0
            source: Icons.url("chevronDown", head.ink, 2.5)
            sourceSize: Qt.size(10, 10)
        }
    }
    MouseArea {
        id: headArea
        anchors.fill: parent
        enabled: head.sortable
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // Takes the focus from a cell open for editing, which keeps its edit.
        onPressed: headArea.forceActiveFocus()
        onClicked: head.clicked()
    }
}
