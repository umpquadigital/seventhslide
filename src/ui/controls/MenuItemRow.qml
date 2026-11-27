import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One item in an AppMenu: an icon (a glyph name from Icons, or an
// ``iconSource`` URL), the label, and optionally a shortcut on the right.
// ``checkable`` items keep a slot for a check before the icon (with
// ``checkSlot`` off, only while checked, so a list of names stays at the
// left and the one picked moves over for its check). ``danger`` is
// red. ``current`` lights it as hover does: the row the keys are on, in a
// list a search field steps through. A ``detail`` goes on a second line (a disabled item says why there);
// the check and icon stay level with the label, not the pair.
Rectangle {
    id: entry
    property string icon: ""
    property string iconSource: ""
    property string label: ""
    property string shortcut: ""
    property string detail: ""
    property bool checkable: false
    property bool checked: false
    /// A checkable item keeps the check's slot while unchecked.
    property bool checkSlot: true
    property bool danger: false
    /// Lit as under the pointer: the row the keys are on.
    property bool current: false
    readonly property bool hovered: area.containsMouse
    signal triggered()

    readonly property color _ink: !entry.enabled ? Style.textFaint
                                : entry.danger ? Style.textDanger
                                : (area.containsMouse || entry.current ? Style.textStrong : Style.textHover)
    readonly property color _glyphInk: !entry.enabled ? Style.textFaint
                                     : entry.danger ? Style.textDanger : Style.iconIdle

    width: parent ? parent.width : implicitWidth
    implicitWidth: layout.implicitWidth + 20
    implicitHeight: entry.detail.length ? layout.implicitHeight + 2 * entry._padY : 28
    readonly property int _padY: 7
    /// Down from the top of the text to centre a ``size`` glyph on the label line.
    function _onLabel(size) {
        return Math.max(0, Math.round((labelText.implicitHeight - size) / 2))
    }
    radius: Style.radiusSmall
    color: (area.containsMouse || entry.current) && entry.enabled ? Style.menuHover : "transparent"

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: entry.detail.length ? entry._padY : 0
        anchors.bottomMargin: entry.detail.length ? entry._padY : 0
        spacing: 10

        Item {
            visible: entry.checkable && (entry.checkSlot || entry.checked)
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            Layout.alignment: entry.detail.length ? Qt.AlignTop : Qt.AlignVCenter
            Layout.topMargin: entry.detail.length ? entry._onLabel(14) : 0
            Image {
                anchors.fill: parent
                visible: entry.checked
                source: Icons.url("check", Style.textBright, 2.5)
                sourceSize: Qt.size(14, 14)
            }
        }
        Image {
            visible: entry.icon.length > 0 || entry.iconSource.length > 0
            // One size in every row, so labels line up down a menu that mixes
            // rows with a second line and rows without.
            Layout.preferredWidth: 14
            Layout.preferredHeight: 14
            Layout.alignment: entry.detail.length ? Qt.AlignTop : Qt.AlignVCenter
            Layout.topMargin: entry.detail.length ? entry._onLabel(14) : 0
            source: entry.iconSource.length ? entry.iconSource
                                           : (entry.icon.length ? Icons.url(entry.icon, entry._glyphInk, 2) : "")
            sourceSize: Qt.size(14, 14)
        }
        Column {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            Text {
                id: labelText
                text: entry.label
                color: entry._ink
                font.pixelSize: Style.fontBody
            }
            Text {
                visible: entry.detail.length > 0
                text: entry.detail
                color: entry.enabled ? Style.textCaption : Style.textWarning
                font.pixelSize: Style.fontSmall
            }
        }
        Text {
            visible: entry.shortcut.length > 0
            Layout.leftMargin: 8
            Layout.alignment: entry.detail.length ? Qt.AlignTop : Qt.AlignVCenter
            Layout.topMargin: entry.detail.length
                              ? Math.max(0, Math.round((labelText.implicitHeight - implicitHeight) / 2)) : 0
            text: entry.shortcut
            color: Style.textFaint
            font.pixelSize: Style.fontSmall
        }
    }
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: entry.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: if (entry.enabled) entry.triggered()
    }
}
