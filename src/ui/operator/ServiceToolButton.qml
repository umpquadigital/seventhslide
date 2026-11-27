pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// A service toolbar button: compact, and pressing it ends a library field edit.
IconToolButton {
    /// Untyped, since ServicePanel makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var panel // ServicePanel
    size: "control"
    strokeWidth: 2
    onPressed: panel.interacted()
}
