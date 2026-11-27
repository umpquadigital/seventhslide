pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// A ribbon button whose split caret opens a menu of the items declared in
// it. An item picked closes the menu. The icon's own press is ``clicked``:
// it may open the menu too (``toggleMenu()``) or do what the button names.
RibbonButton {
    id: button
    /// Its menu is showing.
    property bool open: false
    default property alias items: buttonMenu.items

    menu: true

    function toggleMenu() {
        button.open = !button.open
    }
    onMenuClicked: button.toggleMenu()

    AppMenu {
        id: buttonMenu
        anchorItem: button
        visible: button.open && button.visible
        onVisibleChanged: if (visible) buttonMenu.place()
        // Escape or a press elsewhere closed it: the flag goes too, or the
        // next press on the caret would only "close" it.
        onClosed: button.open = false
    }

    function _close() {
        button.open = false
    }
    Component.onCompleted: {
        for (const item of buttonMenu.items) {
            const row = item as MenuItemRow
            if (row)
                row.triggered.connect(button._close)
        }
    }
}
