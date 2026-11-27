import QtQuick
import QtQuick.Controls

// Dismissal for the app's dropdown menus.
//
// Popup.CloseOnPressOutside cannot be used for a dropdown whose opener toggles
// it. Qt closes the popup while handling the press and then delivers that same
// press to the item underneath, so the opener's own handler sees a closed menu
// and opens it straight back up - the button that opened a menu can never
// close it again. This takes dismissal over instead: while the menu is up it
// covers the window (under the popups, over everything else) and on a press it
// closes the menu, swallowing the press only when it landed on `opener` so the
// opener's toggle never runs. Presses anywhere else fall through untouched, so
// one click still dismisses the menu *and* hits what was clicked.
//
// Give the menu `closePolicy: Popup.CloseOnEscape` and leave
// CloseOnPressOutside off - this replaces it.
//
//   AppButton { id: sortBtn; onMenuClicked: root.sortMenuOpen = !root.sortMenuOpen
//       Popup { id: sortMenu; closePolicy: Popup.CloseOnEscape
//               visible: root.sortMenuOpen; onClosed: root.sortMenuOpen = false }
//       MenuDismissArea { menu: sortMenu; opener: sortBtn }
//   }
MouseArea {
    id: dismissArea

    /// The menu this area dismisses.
    property Popup menu: null
    /// The control that toggles `menu`; presses on it are swallowed.
    property Item opener: null

    function _hitsOpener(x, y) {
        const o = dismissArea.opener
        if (!o || !o.visible)
            return false
        const p = dismissArea.mapToItem(o, x, y)
        return p.x >= 0 && p.y >= 0 && p.x < o.width && p.y < o.height
    }

    parent: Overlay.overlay
    anchors.fill: parent
    // Under the popups, above the rest of the app.
    z: -1
    visible: dismissArea.menu !== null && dismissArea.menu.visible
    enabled: dismissArea.visible
    acceptedButtons: Qt.AllButtons

    onPressed: function (mouse) {
        dismissArea.menu.close()
        mouse.accepted = dismissArea._hitsOpener(mouse.x, mouse.y)
    }
}
