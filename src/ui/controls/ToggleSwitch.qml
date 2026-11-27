import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Bare on/off switch: the knob only, for rows that carry their own label.
// Emits ``toggled``; the caller flips ``checked``.
Item {
    id: sw

    property bool checked: false
    signal toggled()

    width: 36
    height: 18
    Layout.preferredWidth: 36
    Layout.preferredHeight: 18
    Layout.alignment: Qt.AlignVCenter

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: sw.checked ? Style.accent : Style.toggleOff
        border.color: sw.checked ? Style.accent : Style.toggleOffEdge
        Rectangle {
            width: 14
            height: 14
            radius: height / 2
            x: sw.checked ? parent.width - width - 2 : 2
            y: 2
            color: Style.knob
        }
    }
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: sw.toggled()
    }
}
