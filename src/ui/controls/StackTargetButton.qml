pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide
import "../js/Stacks.js" as Stacks

// Toolbar send-to target: arm a stack for library and controller sends.
// Service rundown pins still win. Hidden while only one stack exists.
Item {
    id: root
    required property SessionFacade session

    readonly property bool active: !!(session && session.stacks.all && session.stacks.all.length >= 2)
    readonly property string armedId: session ? session.stacks.armedSendToId : ""
    readonly property var armedStack: session ? Stacks.findById(session.stacks.all, armedId) : null

    visible: active
    implicitWidth: active ? btn.width : 0
    implicitHeight: active ? btn.height : 0
    width: implicitWidth
    height: implicitHeight

    AppButton {
        id: btn
        size: "compact"
        toggled: root.armedId.length > 0
        iconSource: {
            if (!root.session)
                return ""
            if (root.armedStack)
                return Icons.url(root.armedStack.icon, Style.textStrong, 2)
            return Icons.url("sendTo", Style.iconSoft, 2)
        }
        onClicked: {
            if (targetMenu.opened)
                targetMenu.close()
            else
                targetMenu.openAt()
        }
    }

    StackSendMenu {
        id: targetMenu
        objectName: "sendToMenu"
        session: root.session
        anchorItem: btn
        chosenId: root.armedId
        // Picking the armed stack again disarms it.
        onPicked: function (stackId) {
            if (root.session)
                root.session.stacks.armSendTo(stackId === root.armedId ? "" : stackId)
        }
    }
}
