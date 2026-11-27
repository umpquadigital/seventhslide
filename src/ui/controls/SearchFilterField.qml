import QtQuick
import SeventhSlide

// A search field fused with a filter button at its right end: the button is a
// tag glyph, tinted while ``filtering``, showing ``filterLabel`` (the tag
// being filtered by) when there is one. ``filterClicked`` asks the owner to
// open its filter menu, anchored on ``filterButton``. Callers bind ``text``
// and write it back from ``edited``; ``escaped`` fires on Escape.
Rectangle {
    id: field
    property string text: ""
    property string placeholder: qsTr("Search")
    property bool filtering: false
    property string filterLabel: ""
    readonly property alias inputFocused: input.activeFocus
    readonly property alias filterButton: filterButton
    signal edited(string value)
    signal escaped()
    signal filterClicked()

    function blur() {
        input.deselect()
        input.focus = false
    }

    implicitWidth: Style.fieldLongW
    implicitHeight: Style.controlH
    radius: Style.radiusSmall
    color: input.activeFocus ? Style.fieldFaceFocus : Style.fieldFace
    border.color: input.activeFocus ? Style.focusEdge
                : (fieldHover.hovered ? Style.controlEdgeHover : Style.controlEdge)
    clip: true

    HoverHandler {
        id: fieldHover
    }

    Image {
        id: glyph
        x: 9
        anchors.verticalCenter: parent.verticalCenter
        width: 12
        height: 12
        source: Icons.url("search", Style.fieldPlaceholder, 2)
        sourceSize: Qt.size(12, 12)
    }
    TextInput {
        id: input
        anchors.left: glyph.right
        anchors.leftMargin: 7
        anchors.right: filterButton.left
        anchors.rightMargin: 6
        anchors.verticalCenter: parent.verticalCenter
        color: Style.text
        font.pixelSize: Style.fontBody
        selectByMouse: true
        clip: true
        text: field.text
        onTextChanged: if (text !== field.text) field.edited(text)
        Keys.onEscapePressed: function (event) {
            field.escaped()
            event.accepted = true
        }
        Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            visible: !input.text.length && !input.activeFocus
            text: field.placeholder
            color: Style.fieldPlaceholder
            font.pixelSize: Style.fontBody
        }
    }
    Rectangle {
        id: filterButton
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 1
        width: filterRow.implicitWidth + 16
        color: field.filtering ? Style.onTint
             : (filterArea.containsMouse ? Style.btnNeutralHoverTop : "transparent")
        Rectangle {
            width: 1
            height: parent.height
            color: Style.controlEdge
        }
        Row {
            id: filterRow
            anchors.centerIn: parent
            spacing: 5
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 12
                height: 12
                source: Icons.url("tag", field.filtering ? Style.onGlyph : Style.iconIdle, 2)
                sourceSize: Qt.size(12, 12)
            }
            Text {
                visible: field.filterLabel.length > 0
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 90)
                text: field.filterLabel
                color: Style.onInk
                font.pixelSize: Style.fontBody
                elide: Text.ElideRight
            }
            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                source: Icons.url("caret", field.filtering ? Style.onGlyph : Style.iconIdle)
                sourceSize: Qt.size(8, 8)
            }
        }
        MouseArea {
            id: filterArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: field.filterClicked()
        }
    }
}
