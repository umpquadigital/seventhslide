import QtQuick
import SeventhSlide

// The layer behind a modal dialog or sheet. It takes everything aimed at the
// window underneath - clicks of any button, hover and the wheel - so nothing
// behind an open dialog highlights, scrolls or reacts. ``clicked`` fires for a
// left click on the scrim itself; the dialog decides whether that closes it.
// A dialog's card goes above it (declared after it, or as a child).
//
// ``tint`` is how it dims the window, but the window dims only once: while it
// is visible (it and every ancestor) it registers in Modals, and only the
// first tinted scrim shown paints (Modals.dimmer). One stacked over it takes
// input and draws nothing, so stacked dialogs never darken the window further.
Item {
    id: scrim

    signal clicked()

    /// The dim it paints when it is the window's dimmer; transparent, none.
    property color tint: "transparent"

    function _register() {
        if (scrim.visible)
            Modals.add(scrim)
        else
            Modals.remove(scrim)
    }

    onVisibleChanged: scrim._register()
    Component.onCompleted: scrim._register()
    Component.onDestruction: Modals.remove(scrim)

    Rectangle {
        anchors.fill: parent
        color: scrim.tint
        visible: Modals.dimmer === scrim
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        cursorShape: Qt.ArrowCursor
        onClicked: function (mouse) {
            if (mouse.button === Qt.LeftButton)
                scrim.clicked()
        }
        onWheel: function (wheel) {
            wheel.accepted = true
        }
    }
}
