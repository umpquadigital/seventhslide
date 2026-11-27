pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// A field of words someone types - a song's topics, a service's tags: the
// words as removable chips, then room to type another. Enter or a comma adds
// what is typed; Backspace in the empty slot takes the last chip off. The
// field wraps onto more lines as chips are added.
//
// The caller owns the list: ``tags`` is what it shows, ``added(text)`` and
// ``removed(index)`` ask it to change.
Rectangle {
    id: field
    property var tags: []
    property string placeholder: ""
    signal added(string text)
    signal removed(int index)

    /// Test seam: tests focus the field without a click.
    function focusInput() { input.forceActiveFocus() }

    function _addTyped() {
        const text = input.text.trim()
        input.text = ""
        if (text.length)
            field.added(text)
    }

    implicitWidth: Style.fieldLongW
    implicitHeight: Math.max(Style.controlH, flow.implicitHeight + 6)
    radius: Style.radiusSmall
    color: input.activeFocus ? Style.fieldFaceFocus : Style.fieldFace
    border.color: input.activeFocus ? Style.focusEdge
                : (area.containsMouse ? Style.controlEdgeHover : Style.controlEdge)

    // A click on the field's empty space puts the cursor in the slot.
    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.IBeamCursor
        onClicked: input.forceActiveFocus()
    }

    Flow {
        id: flow
        x: 3
        y: 3
        width: parent.width - 6
        spacing: 3

        Repeater {
            model: field.tags
            delegate: TagChip {
                required property string modelData
                required property int index
                text: modelData
                removable: true
                onRemoveClicked: field.removed(index)
            }
        }

        TextInput {
            id: input
            // The rest of the chips' last line, so a click there types; a
            // line of its own when that leaves no room for the placeholder.
            readonly property real minWidth: Math.max(60, hint.implicitWidth + 12)
            readonly property real rest: Math.floor(flow.width - field._lastLineWidth())
            width: rest >= minWidth ? rest : flow.width
            height: 20
            leftPadding: 6
            verticalAlignment: TextInput.AlignVCenter
            color: Style.text
            font.pixelSize: Style.fontBody
            selectByMouse: true
            clip: true
            onAccepted: field._addTyped()
            onActiveFocusChanged: {
                if (!activeFocus)
                    field._addTyped()
            }
            onTextChanged: {
                if (text.indexOf(",") >= 0) {
                    const parts = text.split(",")
                    text = parts.pop()
                    for (const part of parts) {
                        if (part.trim().length)
                            field.added(part.trim())
                    }
                }
            }
            Keys.onPressed: function (event) {
                if (event.key === Qt.Key_Backspace && !input.text.length
                        && field.tags.length > 0) {
                    field.removed(field.tags.length - 1)
                    event.accepted = true
                }
            }

            Text {
                id: hint
                x: input.leftPadding
                anchors.verticalCenter: parent.verticalCenter
                text: field.placeholder
                color: Style.fieldPlaceholder
                font.pixelSize: Style.fontBody
                visible: !input.text.length && !input.activeFocus
            }
        }
    }

    /// How far the chips on the last line reach, so the slot can take the
    /// rest of that line.
    function _lastLineWidth() {
        let lineY = -1
        let right = 0
        for (let i = 0; i < flow.children.length; ++i) {
            const c = flow.children[i]
            if (c === input || !c.visible || c.width <= 0)
                continue
            if (c.y > lineY) {
                lineY = c.y
                right = 0
            }
            right = Math.max(right, c.x + c.width + flow.spacing)
        }
        return right
    }
}
