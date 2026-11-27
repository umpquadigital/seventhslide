import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A GroupRow that switches one thing on and off: its glyph, its name, and a
// ToggleSwitch at the right edge. Emits ``toggled``; the caller flips ``checked``.
GroupRow {
    id: row

    required property string icon
    required property string label
    property bool checked: false
    signal toggled()

    Image {
        Layout.preferredWidth: 14
        Layout.preferredHeight: 14
        source: Icons.url(row.icon, Style.iconIdle, 2)
        sourceSize: Qt.size(14, 14)
    }
    Text {
        Layout.fillWidth: true
        text: row.label
        color: Style.text
        font.pixelSize: Style.fontBody
        elide: Text.ElideRight
    }
    ToggleSwitch {
        checked: row.checked
        onToggled: row.toggled()
    }
}
