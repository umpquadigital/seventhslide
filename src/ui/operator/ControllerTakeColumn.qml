pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// Between a stack's Preview and its Program, a little above their middle:
// Transition, which takes what is cued live by the theme's transition (T),
// and its ▾, a menu that takes it by a Cut or a Fade instead and sets how
// long the Fade takes. A line under it says when the stack is frozen, as the
// take will not show then (a blank goes with its side, and does not hold the
// take back). Nothing cued, nothing can be taken.
Item {
    id: take
    /// Untyped, since ControllerColumn makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var column // ControllerColumn
    /// The focused stack's entry (``stacks.occupied``): frozen.
    readonly property var stack: take.column.focusedEntry

    // A little above the middle of the wells, two fifths of the way down.
    AppButton {
        id: transitionBtn
        objectName: "takeTransition"
        y: Math.round(take.height * 0.4 - transitionBtn.height / 2)
        width: parent.width
        size: "command"
        primary: true
        label: qsTr("Transition")
        splitMenu: true
        menuOpen: takeMenu.visible
        enabled: take.column.canTake
        onPressed: take.column.interacted()
        onClicked: take.column.take()
        onMenuClicked: {
            take.column.interacted()
            if (takeMenu.visible)
                takeMenu.close()
            else
                takeMenu.openAt()
        }

        TakeTransitionMenu {
            id: takeMenu
            objectName: "takeMenu"
            column: take.column
            anchorItem: transitionBtn
        }
    }

    // The take will not show: the stack is frozen. Under Transition, which
    // it does not move.
    Text {
        objectName: "takeHeld"
        anchors.top: transitionBtn.bottom
        anchors.topMargin: 6
        width: parent.width
        visible: text.length > 0
        text: take.stack && take.stack.live && take.stack.frozen
              ? qsTr("Frozen: the take will not show") : ""
        color: Style.textWarning
        font.pixelSize: Style.fontCaption
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }
}
