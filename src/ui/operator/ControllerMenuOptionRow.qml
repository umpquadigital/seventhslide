pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// A row of the controller options menu: a title and hint, and a switch.
Item {
    id: optRow
    property string label: ""
    property string hint: ""
    property bool checked: false
    signal toggled(bool on)

    // The menu's width; as wide as the label and the switch ask, so the menu
    // grows to fit its longest label (AppMenu).
    width: parent ? parent.width : implicitWidth
    implicitWidth: 12 + labelText.implicitWidth + 8 + switchTrack.width + 12
    height: hint.length ? 48 : 32

    Column {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: switchTrack.left
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 1

        Text {
            id: labelText
            width: parent.width
            text: optRow.label
            color: Style.menuInk
            font.pixelSize: Style.fontBody
            elide: Text.ElideRight
        }
        Text {
            visible: optRow.hint.length > 0
            width: parent.width
            text: optRow.hint
            color: Style.textFaint
            font.pixelSize: Style.fontCaption
            wrapMode: Text.WordWrap
        }
    }

    Rectangle {
        id: switchTrack
        anchors.right: parent.right
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        width: 34
        height: 18
        radius: height / 2
        color: optRow.checked ? Style.accent : Style.toggleOff
        border.color: optRow.checked ? Style.sliderKnobEdge : Style.edgeIdle

        Rectangle {
            width: 14
            height: 14
            radius: height / 2
            anchors.verticalCenter: parent.verticalCenter
            x: optRow.checked ? parent.width - width - 2 : 2
            color: Style.knob
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: optRow.toggled(!optRow.checked)
        }
    }

    MouseArea {
        anchors.fill: parent
        anchors.rightMargin: switchTrack.width + 16
        cursorShape: Qt.PointingHandCursor
        onClicked: optRow.toggled(!optRow.checked)
    }
}
