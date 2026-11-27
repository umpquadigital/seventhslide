pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// An icon on the ribbon with a split caret whose menu picks one of
// ``options``, [{value, label}], the current ``value`` checked: bullets,
// line spacing, columns. The icon's press is ``picked(defaultValue)`` when
// there is a ``defaultValue``, or opens the menu. It keeps whether its menu
// is open, as RibbonDropdown does.
RibbonButton {
    id: choice
    property var options: []
    property var value
    /// What a press on the icon itself picks; undefined opens the menu.
    property var defaultValue
    /// Its menu is showing.
    property bool open: false
    signal picked(var value)

    menu: true
    menuOpen: choice.open

    function toggleMenu() {
        choice.open = !choice.open
    }
    onClicked: {
        if (choice.defaultValue === undefined)
            choice.toggleMenu()
        else
            choice.picked(choice.defaultValue)
    }
    onMenuClicked: choice.toggleMenu()

    AppMenu {
        id: choiceMenu
        anchorItem: choice
        minWidth: 120
        visible: choice.open && choice.visible
        onVisibleChanged: if (visible) choiceMenu.place()
        // Escape or a press elsewhere closed it: the flag goes too, or the
        // next press on the icon would only "close" it.
        onClosed: choice.open = false
        Repeater {
            model: choice.options
            delegate: MenuItemRow {
                required property var modelData
                label: String(modelData.label)
                checkable: true
                checked: modelData.value === choice.value
                onTriggered: {
                    choice.open = false
                    choice.picked(modelData.value)
                }
            }
        }
    }
}
