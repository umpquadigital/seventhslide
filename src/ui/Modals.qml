pragma Singleton
import QtQml

// The modal surfaces on screen. Every dialog, sheet and editor takes input
// through a ModalScrim, and each scrim registers itself here while it is
// visible, so nothing has to keep a list of dialogs.
//
// The operator window's shortcuts use ``open``: behind a modal, keys belong to
// the modal (an arrow nudges the selected object) and must never drive the
// live output.
//
// The window dims once, however many modals are stacked: only ``dimmer``
// paints its tint, and every other scrim still takes input but draws nothing.
// The dimmer is the first scrim shown that has a tint, so a confirm or picker
// opened over a sheet leaves the window behind exactly as dark as it was.
QtObject {
    readonly property bool open: shown.length > 0

    /// The visible scrims, the first shown first. Only ModalScrim changes it,
    /// through ``add`` and ``remove``.
    property var shown: []

    readonly property QtObject dimmer: {
        for (const scrim of shown) {
            if (scrim.tint.a > 0)
                return scrim
        }
        return null
    }

    function add(scrim) {
        if (shown.indexOf(scrim) < 0)
            shown = shown.concat([scrim])
    }

    function remove(scrim) {
        if (shown.indexOf(scrim) >= 0)
            shown = shown.filter(function (s) { return s !== scrim })
    }
}
