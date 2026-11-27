import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One row of a stack menu: glyph, label, optional status tags, and a check
// when chosen. The chosen row takes the live service row's blue.
Rectangle {
    id: row
    property string iconSource: ""
    property string label: ""
    property bool chosen: false
    /// Status tags, for menus that list live stacks.
    property bool advancing: false
    property bool blank: false
    property bool frozen: false
    /// Something waits in its Preview.
    property bool cued: false
    /// Ink for the glyph the caller builds into iconSource.
    readonly property color glyphInk: row.chosen ? Style.textOnAccent : Style.iconIdle
    signal clicked()

    component StatusTag: Rectangle {
        property alias text: tagText.text
        property color ink: Style.text
        implicitWidth: tagText.implicitWidth + 10
        implicitHeight: 15
        radius: 2

        Text {
            id: tagText
            anchors.centerIn: parent
            color: parent.ink
            font.pixelSize: Style.fontCaption
        }
    }

    width: parent ? parent.width : 180
    height: 28
    radius: Style.radiusSmall
    color: {
        if (row.chosen)
            return hover.containsMouse ? Style.menuChosenHover : Style.menuChosen
        return hover.containsMouse ? Style.menuHover : "transparent"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        Image {
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            source: row.iconSource
            sourceSize: Qt.size(14, 14)
        }
        Text {
            Layout.fillWidth: true
            text: row.label
            color: row.chosen ? Style.textOnAccent : Style.text
            font.pixelSize: Style.fontBody
            elide: Text.ElideRight
        }
        StatusTag {
            visible: row.advancing
            text: qsTr("Auto")
            color: row.chosen ? Style.statusTagChosenBg : Style.statusTagBg
            ink: row.chosen ? Style.statusTagChosenInk : Style.statusTagInk
        }
        StatusTag {
            visible: row.cued
            text: qsTr("Cued")
            color: row.chosen ? Style.statusTagChosenBg : Style.statusTagBg
            ink: row.chosen ? Style.statusTagChosenInk : Style.statusTagInk
        }
        StatusTag {
            visible: row.blank
            text: qsTr("Blank")
            color: Style.btnDangerHover
            ink: Style.btnDangerInkHover
        }
        StatusTag {
            visible: row.frozen
            text: qsTr("Frozen")
            color: Style.btnFreezeOnBot
            ink: Style.textOnAccent
        }
        Image {
            visible: row.chosen
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            source: Icons.url("check", Style.textOnAccent, 2.5)
            sourceSize: Qt.size(14, 14)
        }
    }

    MouseArea {
        id: hover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: row.clicked()
    }
}
