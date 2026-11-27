pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// Transition's menu (its ▾): take what is cued by a Cut or a Fade in place
// of the theme's transition, and how long the Fade takes, which is kept on
// this computer as it is changed. A take closes the menu; a length leaves it
// open for the Fade.
AppMenu {
    id: menu
    /// Untyped, as ControllerTakeColumn's is (Qt refuses the cycle);
    /// tools/panel_refs.py checks the names used on it.
    required property var column // ControllerColumn
    readonly property int fadeMs: menu.column.session
                                  ? menu.column.session.stacks.takeFadeMs : 500
    minWidth: 220

    /// Take what is cued by ``type`` ("cut" or "fade").
    function takeBy(type) {
        menu.close()
        menu.column.takeWith({ type: type, durationMs: type === "cut" ? 0 : menu.fadeMs })
    }

    MenuItemRow {
        objectName: "takeCut"
        label: TransitionKinds.label("cut")
        enabled: menu.column.canTake
        onTriggered: menu.takeBy("cut")
    }
    MenuItemRow {
        objectName: "takeFade"
        label: TransitionKinds.label("fade")
        shortcut: TransitionKinds.seconds(menu.fadeMs)
        enabled: menu.column.canTake
        onTriggered: menu.takeBy("fade")
    }

    MenuRule {}

    // The Fade's length: typed, or a step at a time with + and -.
    Item {
        width: parent.width
        implicitWidth: fadeLabel.implicitWidth + fadeField.width + 36
        height: 34
        Text {
            id: fadeLabel
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Fade length")
            color: Style.menuInk
            font.pixelSize: Style.fontBody
        }
        RibbonField {
            id: fadeField
            objectName: "takeFadeLength"
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            compact: true
            fieldWidth: 76
            unit: "s"
            displayScale: 0.001
            decimals: 2
            step: 100
            minValue: 100
            maxValue: 10000
            value: menu.fadeMs
            onCommit: function (n) {
                if (menu.column.session)
                    menu.column.session.stacks.setTakeFadeMs(n)
            }
        }
    }
}
