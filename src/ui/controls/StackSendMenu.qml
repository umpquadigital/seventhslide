pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide
import "../js/Stacks.js" as Stacks

// "Send to": the type default, then every stack, the chosen one marked. What
// picking one does is the caller's (``picked``); the menu closes itself.
AppMenu {
    id: menu
    required property SessionFacade session
    /// The stack chosen; "" for the type default.
    property string chosenId: ""
    /// Rows chosen differently (a mixed selection): none is marked.
    property bool mixed: false
    /// The first row: what the type default sends to.
    property string defaultLabel: qsTr("Type default")
    /// ``stackId`` is "" for the type default.
    signal picked(string stackId)

    MenuHeading {
        text: qsTr("Send to")
    }
    StackMenuRow {
        id: defaultRow
        label: menu.defaultLabel
        chosen: !menu.mixed && menu.chosenId === ""
        iconSource: Icons.url("sendTo", defaultRow.glyphInk, 2)
        onClicked: {
            menu.close()
            menu.picked("")
        }
    }
    MenuRule {}
    Repeater {
        model: menu.session ? menu.session.stacks.all : []
        delegate: StackMenuRow {
            id: stackRow
            required property var modelData
            label: Stacks.displayName(stackRow.modelData)
            chosen: !menu.mixed && menu.chosenId === stackRow.modelData.id
            iconSource: Icons.url(stackRow.modelData.icon, stackRow.glyphInk, 2)
            onClicked: {
                menu.close()
                menu.picked(stackRow.modelData.id)
            }
        }
    }
}
