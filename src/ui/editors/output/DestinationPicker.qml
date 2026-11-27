import QtQuick
import SeventhSlide

// The field on a destination that opens its menu (OutputDestinationMenu) to
// pick its target: a monitor, a virtual camera, a Blackmagic device, that
// device's video mode, or a display's sound device. Its label is the form
// row's. The menu keeps whether it is open, as an OptionField's does: a
// target taken closes it, as do Escape, a press anywhere, hiding the field
// (another tab, the window closing) and showing another destination.
DropdownField {
    id: pick

    required property OutputDestinationsState dialog
    /// The destination it picks for.
    required property var dest
    /// Which targets it lists: display, vcam, bmd, bmdmode or sound.
    required property string menuKind
    /// The current pick, as the field shows it.
    required property string valueText
    /// The destination's key for that pick, which the menu checks.
    required property string currentKey

    readonly property string destId: (pick.dest && pick.dest.id) || ""

    text: pick.valueText
    open: menu.visible
    // It was open for the destination shown before.
    onDestIdChanged: menu.close()

    // The targets are asked for again as it opens: a screen or a device may
    // have come or gone since.
    onClicked: {
        if (menu.visible) {
            menu.close()
        } else if (pick.dest) {
            pick.dialog.loadTargets(pick.menuKind)
            menu.openAt()
        }
    }

    OutputDestinationMenu {
        id: menu
        dialog: pick.dialog
        kind: pick.menuKind
        destId: pick.destId
        currentKey: pick.currentKey
        anchorItem: pick
        minWidth: Math.max(196, pick.width)
    }
}
