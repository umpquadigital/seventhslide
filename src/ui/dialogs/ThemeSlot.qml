import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One of an output's theme choices in a theme map: a tile with the theme's
// thumbnail (an empty frame for Inherit, or faded, the theme it inherits when
// the map knows it), what it is (Text, Background) and its name; clicking
// opens the picker. Shared by the song editor's and the
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
    /// The map's inherit choice the slot holds, or null for a theme of its own.
    readonly property var inheritChoice: slot.map.inheritChoice(slot.selectedId)
    readonly property bool isInherit: slot.inheritChoice !== null
    /// The theme the slot shows: its own, or the one it inherits ("" for none known).
    readonly property string shownId: slot.isInherit
                                      ? slot.map.inheritedId(slot.outputId, slot.pick.category,
                                                             slot.pick.field, slot.selectedId)
                                      : slot.selectedId
    readonly property string displayName: slot.map.displayName(slot.outputName, slot.kind,
                                                               slot.shownId)
    readonly property string thumbUrl: slot.map.thumbUrl(slot.outputName, slot.kind,
                                                         slot.shownId)
    readonly property bool isUnknown: slot.shownId.length > 0
                                      && slot.displayName === qsTr("Unknown")
    /// The second line: the theme's name, or the inherit choice's, with the
    /// theme it inherits after it.
    readonly property string nameText: {
        if (!slot.isInherit)
            return slot.displayName
        const choiceName = String(slot.inheritChoice.slotName || "")
        return slot.shownId.length ? qsTr("%1 · %2").arg(choiceName).arg(slot.displayName)
                                   : choiceName
    }
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
                visible: slot.shownId.length > 0 && !slot.isUnknown
                // An inherited theme is not the slot's own.
                opacity: slot.isInherit ? Style.inheritedOpacity : 1
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
                text: slot.nameText
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
