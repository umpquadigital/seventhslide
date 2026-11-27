pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// A field that holds a set of choices - which item types a stack receives:
// it reads as a summary ("Bibles", "Images, Videos", "Nothing") and opens a
// checklist that stays open while boxes are ticked. ``model`` is a list of
// { id, label, icon, locked } (icon: an image URL; a locked item cannot be
// changed); ``checkedIds`` the ids ticked.
// The caller owns the set: ``toggled(id)`` asks it to flip one. An
// ``actionLabel`` adds an item under a rule that closes the menu and emits
// ``actionTriggered`` (a song credit's "Other…").
DropdownField {
    id: field
    property var model: []
    property var checkedIds: []
    /// What the field says when nothing is ticked.
    property string noneText: qsTr("Nothing")
    /// What it says when more than ``maxNamed`` are ticked; %1 is the count.
    property string manyText: qsTr("%1 selected")
    property int maxNamed: 3
    property string actionLabel: ""
    readonly property bool menuOpen: menu.visible
    signal toggled(string id)
    signal actionTriggered()

    function isChecked(id) {
        return (field.checkedIds || []).indexOf(id) >= 0
    }
    /// The summary the field shows.
    function summary() {
        const names = []
        for (const m of field.model || []) {
            if (field.isChecked(m.id))
                names.push(m.label)
        }
        if (!names.length)
            return ""
        return names.length > field.maxNamed ? field.manyText.arg(names.length)
                                             : names.join(", ")
    }

    readonly property string _said: field.summary()
    text: field._said.length ? field._said : field.noneText
    placeholder: !field._said.length
    open: menu.visible
    onClicked: menu.visible ? menu.close() : menu.openAt()

    AppMenu {
        id: menu
        anchorItem: field
        minWidth: field.width

        Repeater {
            model: field.model
            delegate: MenuItemRow {
                required property var modelData
                checkable: true
                checked: field.isChecked(modelData.id)
                iconSource: modelData.icon || ""
                label: modelData.label
                enabled: !modelData.locked
                onTriggered: field.toggled(modelData.id)
            }
        }
        MenuRule {
            visible: field.actionLabel.length > 0
        }
        MenuItemRow {
            visible: field.actionLabel.length > 0
            // Keeps the check slot, so the label lines up with the items.
            checkable: true
            label: field.actionLabel
            onTriggered: {
                menu.close()
                field.actionTriggered()
            }
        }
    }
}
