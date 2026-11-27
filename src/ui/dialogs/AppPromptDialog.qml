import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Single-line text prompt on the shared AppDialog card: new folder, rename,
// section title, "save as" name. Read the answer from `value` in onAccepted.
// The accept says what it does: Create for something new in the library,
// Add for something put into what is open (a divider, a credit), Rename for
// a rename.
//
//   AppPromptDialog {
//       title: qsTr("New folder")
//       acceptLabel: qsTr("Create")
//       onAccepted: session.createFolder(value)
//   }
AppDialog {
    id: root

    /// Muted label above the field. Omit for a bare one-field prompt.
    property string label: ""
    property string placeholder: ""
    /// Fixed trailing text the field cannot edit, e.g. a locked file extension.
    property string suffix: ""
    /// What the field holds.
    property string value: ""
    readonly property string trimmedValue: root.value.trim()
    /// Grow the card to fit the text, capped by maxAutoWidth and the window.
    property bool autoSize: true
    property int minAutoWidth: 320
    property int maxAutoWidth: 640
    /// Block accept (and Enter) while the field is empty.
    property bool requireValue: true
    /// A password: shown as dots, even while it is typed, and the card does not
    /// grow with it.
    property bool password: false

    /// Show with a starting value, pre-selected for overtype.
    function ask(initial) {
        root.value = initial === undefined ? "" : String(initial)
        root.open = true
    }

    // A password is not left in the field once the card closes: after
    // onAccepted has read it, which runs once the card has closed.
    function _forgetPassword() {
        if (!root.open && root.password)
            root.value = ""
    }
    onOpenChanged: {
        if (root.open) {
            Qt.callLater(root._takeFieldFocus)
            return
        }
        field.clearFocus()
        if (root.password)
            Qt.callLater(root._forgetPassword)
    }

    /// Deferred so it lands after AppDialog's own _ensureFocus grab, which
    /// runs in the same frame and would otherwise keep the caret on the card.
    function _takeFieldFocus() {
        if (!root.open)
            return
        field.focusInput()
        field.selectAll()
    }

    danger: false
    acceptLabel: qsTr("OK")
    acceptEnabled: !root.requireValue || root.trimmedValue.length > 0
    cardWidth: root.autoSize && !root.password
               ? Math.round(Math.max(root.minAutoWidth,
                                     Math.min(root.maxAutoWidth,
                                              promptMetrics.width + 60)))
               : Style.modalWidth

    TextMetrics {
        id: promptMetrics
        font.pixelSize: Style.fontBody
        text: root.value + root.suffix
    }

    Text {
        Layout.fillWidth: true
        visible: root.label.length > 0
        text: root.label
        color: Style.textMuted
        font.pixelSize: Style.fontSmall
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4

        DialogField {
            id: field
            Layout.fillWidth: true
            text: root.value
            placeholder: root.placeholder
            password: root.password
            echoMode: root.password ? TextInput.Password : TextInput.Normal
            onEdited: function (v) { root.value = v }
            onSubmitted: root.submit()
        }

        Text {
            Layout.alignment: Qt.AlignVCenter
            visible: root.suffix.length > 0
            text: root.suffix
            color: Style.textMuted
            font.pixelSize: Style.fontBody
        }
    }
}
