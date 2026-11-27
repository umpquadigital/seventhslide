import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Icon-only toolbar button: an AppButton that draws ``icon``, a glyph name from
// Icons, in the button's own ink. ``strokeWidth`` is the outline weight for
// stroke glyphs; the operator columns and the theme picker draw lighter (2).
AppButton {
    property string icon: ""
    property real strokeWidth: 2.5
    Layout.alignment: Qt.AlignVCenter
    iconSource: icon.length ? Icons.url(icon, ink, strokeWidth) : ""
}
