import QtQuick
import QtQuick.Controls
import SeventhSlide

// A dropdown menu: the one menu face (dark, outlined, rounded) around a
// column of MenuItemRow / MenuHeading / MenuRule declared inside. Open it
// under ``anchorItem`` with ``openAt``; ``alignRight`` lines its right edge up
// with the anchor's (for buttons at the right of a row), otherwise its left.
//
// Items do not close the menu: the caller closes it in ``onTriggered``, and a
// checklist leaves it open.
//
// Dismissal follows MenuDismissArea's pattern: give the opener as ``opener``
// and the menu closes on a press anywhere, without the opener reopening it.
// It closes too when ``anchorItem`` is hidden (its page left, its window
// closed), as it would hang from nothing.
//
// A menu taller than ``maxHeight`` (by default, the window less a margin),
// or than the room on the side of the anchor it opens on, scrolls.
Popup {
    id: menu
    default property alias items: column.data
    property Item anchorItem: null
    property Item opener: anchorItem
    property bool alignRight: false
    /// Never narrower than this; a field's menu passes the field's width.
    property real minWidth: 196
    /// Never taller than this; the items scroll past it.
    property real maxHeight: Overlay.overlay ? Overlay.overlay.height - 8 : 100000
    /// The room on the side of the anchor ``place`` chose.
    property real _room: 100000
    readonly property real _limit: Math.min(menu.maxHeight, menu._room)

    parent: Overlay.overlay
    padding: 4
    modal: false
    dim: false
    // Holds the keyboard while open, so Escape closes the menu and not the
    // window under it; focus goes back where it was on close.
    focus: true
    closePolicy: Popup.CloseOnEscape
    // As wide as the widest item asks to be. Not column.implicitWidth: a
    // Column sizes from its children's widths, and they take the menu's.
    implicitWidth: Math.max(menu.minWidth, menu._itemsWidth + Style.scrollGutter(body)
                                           + menu.leftPadding + menu.rightPadding)
    readonly property real _itemsWidth: {
        let w = 0
        for (const child of column.children)
            w = Math.max(w, child.implicitWidth)
        return w
    }

    function openAt() {
        // Laid out first: rows that just changed (a Repeater over options a
        // setting moved) count in the height only at the column's next
        // layout, and the menu would be placed for the old ones.
        column.forceLayout()
        menu.place()
        menu.open()
    }

    /// Under the anchor, or over it when it fits only there. One that fits
    /// the window but neither side moves up over its anchor, all of it in
    /// sight; one taller than the window takes the roomier side and scrolls
    /// there, so a long list never covers its field. Inside the window
    /// either way.
    function place() {
        const o = Overlay.overlay
        const a = menu.anchorItem
        if (!o || !a)
            return
        const p = a.mapToItem(o, 0, a.height)
        const top = a.mapToItem(o, 0, 0).y
        const w = menu.implicitWidth
        let x = menu.alignRight ? p.x + a.width - w : p.x
        x = Math.max(4, Math.min(x, o.width - w - 4))
        // From the items, not implicitHeight: that is what the room limits.
        const full = column.implicitHeight + menu.topPadding + menu.bottomPadding
        const wanted = Math.min(menu.maxHeight, full)
        const below = o.height - 4 - (p.y + 4)
        const above = top - 4 - 4
        let room = 100000
        let y = p.y + 4
        if (wanted > below && (full <= menu.maxHeight || above > below)) {
            if (full > menu.maxHeight)
                room = Math.max(0, above)
            y = Math.max(4, top - 4 - Math.min(wanted, room))
        } else if (wanted > below) {
            room = Math.max(0, below)
        }
        if (menu._room !== room)
            menu._room = room
        menu.x = x
        menu.y = y
    }
    // Rows that come or go while it is open (a list filling, a search) move
    // it, so it stays inside the window. Afterwards, not inside the change:
    // the room it sets changes the height again.
    onImplicitHeightChanged: if (menu.visible) Qt.callLater(menu.place)
    onImplicitWidthChanged: if (menu.visible) Qt.callLater(menu.place)

    /// The item it hangs from is hidden, itself or by what it is in.
    readonly property bool _anchorHidden: !!menu.anchorItem && !menu.anchorItem.visible
    on_AnchorHiddenChanged: if (menu._anchorHidden) menu.close()

    background: Rectangle {
        radius: Style.radiusLarge
        color: Style.menuBg
        border.color: Style.menuEdge
    }

    // Covers the window under the menu while it is open, and is in the
    // overlay (the menu's own parent) only then. Parked there while closed,
    // it left the overlay as the item under the pointer in the controls'
    // tests, and clicks on the page never arrived.
    property MenuDismissArea _dismiss: MenuDismissArea {
        parent: menu.visible ? menu.parent : null
        menu: menu
        opener: menu.opener
    }
    /// The items are taller than ``maxHeight``, or the room, allows.
    readonly property bool _scrolls: column.implicitHeight
                                     > menu._limit - menu.topPadding - menu.bottomPadding
    // The bar is made only while the items scroll. One attached from the
    // start saw the closed menu's empty body as overflowing and kept its
    // gutter (and a track) on menus that fit.
    on_ScrollsChanged: menu._syncBar()
    Component.onCompleted: menu._syncBar()
    function _syncBar() {
        const bar = body.ScrollBar.vertical
        if (menu._scrolls && !bar) {
            body.ScrollBar.vertical = barComponent.createObject(body)
        } else if (!menu._scrolls && bar) {
            body.ScrollBar.vertical = null
            bar.destroy()
            body.contentY = 0
        }
    }
    Component {
        id: barComponent
        AppScrollBar {}
    }

    contentItem: Flickable {
        id: body
        implicitHeight: Math.min(column.implicitHeight,
                                 menu._limit - menu.topPadding - menu.bottomPadding)
        contentWidth: width - Style.scrollGutter(body)
        contentHeight: column.implicitHeight
        clip: menu._scrolls
        interactive: menu._scrolls
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: body.width - Style.scrollGutter(body)
        }
    }
}
