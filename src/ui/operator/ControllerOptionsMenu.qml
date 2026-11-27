pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// The controller's options menu (the ⋯ button): auto advance, follow live,
// auto scroll, and clearing the stack.
AppMenu {
    id: ctrlMenu
    /// Untyped, since ControllerColumn makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var column // ControllerColumn
    /// The button it drops from.
    required property Item button
    anchorItem: ctrlMenu.button
    alignRight: true
    minWidth: 220

    MenuHeading {
        text: qsTr("Line Controller")
    }

    ControllerMenuOptionRow {
        label: qsTr("Follow live line")
        checked: ctrlMenu.column.autoScroll
        onToggled: function (on) {
            ctrlMenu.column.autoScroll = on
            if (on)
                ctrlMenu.column.followLiveAfterNav(true)
        }
    }

    MenuRule {}

    MenuHeading {
        text: qsTr("Auto-advance")
    }

    Item {
        width: parent.width
        height: 32
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Interval")
            color: Style.menuInk
            font.pixelSize: Style.fontBody
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            DialogField {
                id: intervalField
                width: 48
                horizontalAlignment: TextInput.AlignHCenter
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 1; top: 999 }
                text: String(ctrlMenu.column.autoAdvanceIntervalSec)
                onCommitted: function (v) {
                    const n = parseInt(v, 10)
                    if (n >= 1)
                        ctrlMenu.column.patchFocusedAdvance({ intervalSec: n })
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("s")
                color: Style.textMuted
                font.pixelSize: Style.fontBody
            }
        }
    }

    ControllerMenuOptionRow {
        label: qsTr("Loop")
        checked: ctrlMenu.column.autoAdvanceLoop
        onToggled: function (on) { ctrlMenu.column.patchFocusedAdvance({ loop: on }) }
    }
    ControllerMenuOptionRow {
        label: qsTr("Continue through the service")
        hint: qsTr("Skips items that belong to other stacks.")
        checked: ctrlMenu.column.autoAdvanceThrough
        onToggled: function (on) { ctrlMenu.column.patchFocusedAdvance({ through: on }) }
    }
    ControllerMenuOptionRow {
        label: qsTr("Stop at dividers")
        checked: ctrlMenu.column.autoAdvanceStopAtDividers
        onToggled: function (on) { ctrlMenu.column.patchFocusedAdvance({ stopAtDividers: on }) }
    }

    // "Clear Stack" acts on the focused stack.
    Column {
        id: menuStacks
        width: parent.width
        visible: ctrlMenu.column.stackTabsVisible
        spacing: 0

        MenuRule {}

        MenuHeading {
            text: qsTr("Stacks")
        }

        ControllerMenuDangerRow {
            label: qsTr("Clear Stack")
            onActivated: {
                ctrlMenu.close()
                // The stack the Controller shows, and no other.
                if (ctrlMenu.column.session && ctrlMenu.column.focusedEntry)
                    ctrlMenu.column.session.stacks.clearStack(ctrlMenu.column.focusedEntry.id)
            }
        }

        ControllerMenuDangerRow {
            label: qsTr("Clear all stacks")
            onActivated: {
                ctrlMenu.close()
                if (ctrlMenu.column.session)
                    ctrlMenu.column.session.stacks.clearAllStacks()
            }
        }
    }
}
