pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// A choice on the ribbon: the app's dropdown field over its menu. ``options``
// are [{value, label}] or plain strings; ``value`` is the current one;
// ``picked(value)`` asks for a change. ``label`` is the quiet caption before
// the field. The dropdown keeps whether its menu is open; Escape or a press
// anywhere shuts it, and so does hiding the field (the window closing).
Row {
    id: drop
    property string label: ""
    property var options: []
    property var value
    property int fieldWidth: Style.fieldMediumW
    /// Its menu is showing.
    property bool open: false
    /// A hint said quietly at the field's right.
    property string detail: ""
    /// A glyph before the text; none when empty.
    property string icon: ""
    /// In the sidebar: the shorter control height.
    property bool compact: false
    /// On the tab row, not the ribbon: the app's control height.
    property bool bar: false
    signal picked(var value)

    readonly property string currentLabel: {
        for (let i = 0; i < drop.options.length; ++i) {
            const o = drop.options[i]
            if (typeof o === "string" ? o === String(drop.value) : o.value === drop.value)
                return typeof o === "string" ? o : String(o.label)
        }
        return drop.value === undefined || drop.value === null ? "" : String(drop.value)
    }

    spacing: 6
    height: drop.compact ? Style.btnCompactH : drop.bar ? Style.controlH : Style.ribbonH
    opacity: enabled ? 1 : Style.offOpacity

    Text {
        visible: drop.label.length > 0
        anchors.verticalCenter: parent.verticalCenter
        text: drop.label
        color: Style.textCaption
        font.pixelSize: drop.compact ? Style.fontSmall : Style.ribbonCaption
    }
    DropdownField {
        id: fieldItem
        width: drop.fieldWidth
        height: drop.height
        text: drop.currentLabel
        detail: drop.detail
        icon: drop.icon
        fontSize: drop.compact || drop.bar ? Style.fontBody : Style.ribbonFont
        open: drop.open
        enabled: drop.enabled
        onClicked: drop.open = !drop.open
    }
    AppMenu {
        id: menu
        anchorItem: fieldItem
        minWidth: fieldItem.width
        visible: drop.open && drop.visible
        onVisibleChanged: if (visible) menu.place()
        // Escape or a press elsewhere closed it: the flag goes too, or the
        // next press on the field would only "close" it.
        onClosed: drop.open = false
        Repeater {
            // Built when it opens: a font list is a long one, and there are several.
            model: drop.open ? drop.options : []
            delegate: MenuItemRow {
                required property var modelData
                readonly property var itemValue: typeof modelData === "string" ? modelData : modelData.value
                label: typeof modelData === "string" ? modelData : String(modelData.label)
                checkable: true
                checked: itemValue === drop.value
                onTriggered: {
                    drop.open = false
                    drop.picked(itemValue)
                }
            }
        }
    }
}
