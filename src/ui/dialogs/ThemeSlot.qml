import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One of an output's theme choices in a theme map: a tile with the theme's
// thumbnail (an empty frame for Inherit), what it is (Text, Background) and
// its name; clicking opens the picker. Shared by the song editor's and the
// service settings' Themes pages, which each hand it their ThemeMapEditor and
// one of its slots. The caller gives it a width; two sit side by side.
Rectangle {
    id: slot
    required property ThemeMapEditor map
    /// One of ``map.slotGroups``' slots: {category, field, kind, label}.
    required property var pick
    property string outputId: ""
    property string outputName: ""
    readonly property string kind: String(slot.pick.kind || "")
    readonly property string selectedId: slot.map.themeId(slot.outputId, slot.pick.category,
                                                          slot.pick.field)
    readonly property string displayName: slot.map.displayName(slot.outputName, slot.kind,
                                                               slot.selectedId)
    readonly property string thumbUrl: slot.map.thumbUrl(slot.outputName, slot.kind,
                                                         slot.selectedId)
    readonly property bool isInherit: slot.selectedId.length === 0
    readonly property bool isUnknown: !slot.isInherit && slot.displayName === qsTr("Unknown")
    readonly property int thumbW: 68
    readonly property int thumbH: 38

    implicitWidth: 200
    implicitHeight: slot.thumbH + 12
    radius: Style.radiusSmall
    color: Style.fieldFace
    border.color: slotHover.containsMouse ? Style.controlEdgeHover : Style.controlEdge

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 10
        spacing: 10

        Rectangle {
            Layout.preferredWidth: slot.thumbW
            Layout.preferredHeight: slot.thumbH
            radius: 2
            color: Style.thumbWellDark
            border.color: Style.controlEdge
            clip: true

            Image {
                id: slotThumb
                anchors.fill: parent
                anchors.margins: 1
                visible: !slot.isInherit && !slot.isUnknown
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: true
                sourceSize: Qt.size(slot.thumbW * 2, slot.thumbH * 2)
                source: slot.thumbUrl
            }
            Text {
                anchors.centerIn: parent
                visible: slot.isUnknown
                text: "?"
                color: Style.textCaption
                font.pixelSize: Style.fontBody
            }
        }
        Column {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 1
            Text {
                width: parent.width
                text: String(slot.pick.label || "")
                color: Style.textCaption
                font.pixelSize: Style.fontSmall
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: slot.isInherit ? slot.map.inheritShort : slot.displayName
                color: slot.isInherit ? Style.textSoft : Style.textBright
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
        Image {
            Layout.preferredWidth: 10
            Layout.preferredHeight: 10
            source: Icons.url("chevronDown", Style.chevronInk, 2.5)
            sourceSize: Qt.size(10, 10)
        }
    }

    MouseArea {
        id: slotHover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: slot.map.openPicker(slot.outputId, slot.outputName, slot.pick)
    }
}
