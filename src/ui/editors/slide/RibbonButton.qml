import QtQuick
import SeventhSlide

// A ribbon button: the app's control-height button, an icon with or without
// a label, that is switched on (``on``) in the app's one tint. ``menu`` adds
// the split caret that opens a menu (``menuClicked``).
AppButton {
    id: button
    property string icon: ""
    property bool on: false
    property bool menu: false
    /// A tooltip; the label when there is none.
    property string hint: ""

    size: "ribbon"
    iconSource: button.icon.length ? Icons.url(button.icon, button.ink, 2) : ""
    toggled: button.on
    toggledKind: "on"
    splitMenu: button.menu
    // Out of any field first, so what it holds lands on what it was typed for.
    onPressed: button.forceActiveFocus()
}
