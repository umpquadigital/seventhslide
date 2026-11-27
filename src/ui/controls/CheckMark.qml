import QtQuick
import SeventhSlide

// Bare check box: the mark only, for rows that carry their own label and take
// their own clicks. ``partial`` shows a bar: some of what it stands for is
// checked. A disabled one is empty and faint: nothing there to check.
Rectangle {
    id: mark

    property bool checked: false
    property bool partial: false

    readonly property bool filled: mark.enabled && (mark.checked || mark.partial)

    implicitWidth: 15
    implicitHeight: 15
    radius: Style.radiusSmall
    color: mark.filled ? Style.accent : mark.enabled ? Style.dialogField : "transparent"
    border.color: mark.filled ? Style.btnPrimaryHoverTop
                : mark.enabled ? Style.textFaint : Style.controlEdge

    Image {
        visible: mark.enabled && mark.checked
        anchors.centerIn: parent
        source: Icons.url("check", Style.textOnAccent, 2.5)
        sourceSize: Qt.size(11, 11)
    }
    Rectangle {
        visible: mark.enabled && mark.partial && !mark.checked
        anchors.centerIn: parent
        width: 7
        height: 2
        radius: 1
        color: Style.textOnAccent
    }
}
