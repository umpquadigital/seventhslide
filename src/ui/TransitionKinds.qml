pragma Singleton

import QtQuick

// The transitions there are, as the operator reads them: their names, the
// sides a moving one comes from, and which move. Their values are core's
// (model::to_string of TransitionType and TransitionDirection).
QtObject {
    id: kinds
    /// [{value, label}], in the order they are offered.
    readonly property var types: [
        { value: "cut", label: qsTr("Cut") },
        { value: "fade", label: qsTr("Fade") },
        { value: "fade_through_black", label: qsTr("Fade through black") },
        { value: "push", label: qsTr("Push") },
        { value: "wipe", label: qsTr("Wipe") },
        { value: "split", label: qsTr("Split") },
        { value: "zoom", label: qsTr("Zoom") },
        { value: "cover", label: qsTr("Cover") },
        { value: "reveal", label: qsTr("Reveal") }
    ]
    /// [{value, label}]: where a moving one comes from.
    readonly property var directions: [
        { value: "right", label: qsTr("From right") },
        { value: "left", label: qsTr("From left") },
        { value: "up", label: qsTr("From top") },
        { value: "down", label: qsTr("From bottom") }
    ]

    /// Whether ``type`` moves, and so has a side it comes from (Zoom grows
    /// from the middle).
    function moves(type) {
        return ["push", "wipe", "split", "cover", "reveal"].indexOf(String(type)) >= 0
    }

    /// A length of ``ms`` milliseconds, in seconds: "0.5 s".
    function seconds(ms) {
        return qsTr("%1 s").arg(Math.round(ms / 10) / 100)
    }

    /// ``type``'s name; a cut for one not known, as core reads it.
    function label(type) {
        for (const kind of kinds.types) {
            if (kind.value === type)
                return kind.label
        }
        return kinds.types[0].label
    }
}
