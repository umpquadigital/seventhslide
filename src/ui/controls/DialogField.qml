import QtQuick
import SeventhSlide

// Single-line text field in a dialog. Callers bind ``text`` to their value and
// write it back from ``edited``, which fires on every keystroke.
//
// The field shows the caller's value, with one exception: a change that the
// caller makes while handling ``edited`` (trimming a name, clamping a port) is
// not pushed back into the text being typed, which would move the caret and
// eat keystrokes. The field shows that value once editing ends. A change from
// anywhere else (a chip, a reload) shows at once, even mid-edit.
//
// ``committed`` fires when editing ends (Enter, or leaving the field) with the
// text as it stands, for callers that save a name once rather than per key.
//
// ``password`` echoes dots once editing ends. ``echoMode: TextInput.Password``
// keeps the dots while it is typed too, for a field the caret does not leave
// (a prompt's one field). ``escapeClearsFocus`` (the song editor) makes Escape
// leave the field so the dialog shell receives the next key, where otherwise
// Escape goes straight to the dialog. ``stepsKeys`` hands Up and Down to
// ``stepped`` (-1, 1), for a field that steps through a list under it.
// ``nextKeys`` names characters that emit ``nextTyped`` rather than being
// typed, for one of a row of fields (a date's "/" moves on to the next part).
//
// A text field outside the control kit is one of these, so they have one
// height, one face and one focus edge. The few with a part of their own are
// listed, with why, in tools/text_fields.py.
Rectangle {
    id: field
    property string text: ""
    property string placeholder: ""
    /// Characters the field takes; the default is TextInput's (no real limit).
    property int maximumLength: 32767
    property bool password: false
    property int echoMode: field.password ? TextInput.PasswordEchoOnEdit : TextInput.Normal
    property bool escapeClearsFocus: false
    /// Up and Down emit ``stepped`` rather than move the caret.
    property bool stepsKeys: false
    /// Characters that emit ``nextTyped`` rather than being typed.
    property string nextKeys: ""
    /// What the field takes (a number's range), as TextInput's.
    property alias validator: input.validator
    property alias inputMethodHints: input.inputMethodHints
    property alias horizontalAlignment: input.horizontalAlignment
    /// Whether the caret is in the field.
    readonly property alias inputFocused: input.activeFocus
    signal edited(string value)
    signal committed(string value)
    /// Enter was pressed in the field (and not focus moved away): for a field
    /// whose Enter does something, as a link's looks it up.
    signal submitted(string value)
    /// Escape left the field (``escapeClearsFocus`` only).
    signal escaped()
    /// Up (-1) or Down (1) was pressed (``stepsKeys`` only).
    signal stepped(int by)
    /// One of ``nextKeys`` was typed.
    signal nextTyped()

    // True while ``edited`` runs: a ``text`` change then is the caller's
    // answer to the keystroke, not news.
    property bool _emitting: false

    implicitHeight: Style.controlH
    radius: Style.radiusSmall
    color: input.activeFocus ? Style.fieldFaceFocus : Style.fieldFace
    border.color: input.activeFocus ? Style.focusEdge
                : (fieldHover.containsMouse ? Style.controlEdgeHover : Style.controlEdge)

    onTextChanged: {
        if (!field._emitting && input.text !== field.text)
            input.text = field.text
    }

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 9
        anchors.rightMargin: 9
        verticalAlignment: TextInput.AlignVCenter
        color: Style.text
        font.pixelSize: Style.fontBody
        echoMode: field.echoMode
        maximumLength: field.maximumLength
        selectByMouse: true
        clip: true
        // Tab moves on to the next field (a table's next cell, say).
        activeFocusOnTab: true
        Component.onCompleted: text = field.text
        onTextChanged: {
            if (text === field.text)
                return
            field._emitting = true
            try {
                field.edited(text)
            } finally {
                field._emitting = false
            }
        }
        onEditingFinished: field.committed(text)
        onAccepted: field.submitted(text)
        onActiveFocusChanged: {
            if (!activeFocus && text !== field.text)
                text = field.text
        }
        Keys.onPressed: function (event) {
            if (field.nextKeys.length && event.text.length === 1
                    && field.nextKeys.indexOf(event.text) >= 0) {
                event.accepted = true
                field.nextTyped()
            }
        }
        Keys.onEscapePressed: function (event) {
            if (!field.escapeClearsFocus) {
                event.accepted = false
                return
            }
            field.clearFocus()
            event.accepted = true
            field.escaped()
        }
        Keys.onUpPressed: function (event) {
            event.accepted = field.stepsKeys
            if (field.stepsKeys)
                field.stepped(-1)
        }
        Keys.onDownPressed: function (event) {
            event.accepted = field.stepsKeys
            if (field.stepsKeys)
                field.stepped(1)
        }
    }
    Text {
        anchors.fill: parent
        anchors.leftMargin: 9
        anchors.rightMargin: 9
        verticalAlignment: Text.AlignVCenter
        text: field.placeholder
        color: Style.fieldPlaceholder
        font.pixelSize: Style.fontBody
        visible: !input.text.length && !input.activeFocus
    }
    MouseArea {
        id: fieldHover
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.IBeamCursor
        acceptedButtons: Qt.NoButton
    }

    /// Test seam: tests focus the field without a click.
    function focusInput() { input.forceActiveFocus() }
    function selectAll() { input.selectAll() }
    function clearFocus() {
        input.deselect()
        input.focus = false
    }
}
