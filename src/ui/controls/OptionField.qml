pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// A dropdown over ``options`` (``[{value, label, note}]``), showing the one
// whose value is ``value``; each option's note sits at its right in the
// menu. ``picked`` asks for another; the caller sets ``value``.
DropdownField {
    id: field
    property var options: []
    property var value
    signal picked(var value)

    implicitWidth: Style.fieldMediumW
    text: {
        for (const o of field.options) {
            if (o.value === field.value)
                return o.label
        }
        return ""
    }
    open: menu.visible
    onClicked: menu.visible ? menu.close() : menu.openAt()

    AppMenu {
        id: menu
        anchorItem: field
        minWidth: field.width
        Repeater {
            model: field.options
            delegate: MenuItemRow {
                required property var modelData
                checkable: true
                checked: modelData.value === field.value
                label: modelData.label
                shortcut: modelData.note || ""
                onTriggered: {
                    menu.close()
                    field.picked(modelData.value)
                }
            }
        }
    }
}
