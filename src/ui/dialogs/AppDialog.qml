import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import SeventhSlide

// Shared shell for the app's small modal dialogs - confirms, prompts, pick-one
// sheets. Scrim over the whole window, one centered card: title, optional muted
// blurb, caller-supplied body, right-aligned Cancel / accept footer.
//
// Chrome comes from Style so these read as the same family as the big settings
// sheets rather than the platform MessageDialog.
//
//   AppDialog {
//       title: qsTr("Delete output")
//       blurb: qsTr("Delete this output? Themes on it will be removed.")
//       onAccepted: root.confirmRemoveOutput()
//   }
Item {
    id: root

    property bool open: false
    property string title: ""
    /// Muted line under the title - the whole message for a plain confirm.
    property string blurb: ""
    property int cardWidth: Style.modalWidth
    /// Cap a tall body; 0 sizes the card to its content.
    property int maxCardHeight: 0
    property bool closeOnScrim: true
    /// Turn Escape off while a child dialog is stacked on top of this card - 
    /// two enabled window shortcuts on the same key are ambiguous and Qt fires
    /// neither, so the nested card could not be dismissed.
    property bool escapeEnabled: true

    property string acceptLabel: qsTr("Delete")
    property string cancelLabel: qsTr("Cancel")
    property bool acceptEnabled: true
    /// Destructive accept (delete / remove). Primary chrome when false.
    property bool danger: true
    /// Message-only: a single Close button and no accept.
    property bool acknowledge: false

    /// Body rows, laid out in a ColumnLayout under the blurb.
    default property alias body: bodyCol.data

    signal accepted()
    signal closed()

    function close() {
        if (root.open)
            root.open = false
    }

    /// MessageDialog-style call site: one-off message, then show.
    function ask(text) {
        root.blurb = text || ""
        root.open = true
    }

    function submit() {
        if (!root.acceptEnabled || root.acknowledge)
            return
        root.open = false
        root.accepted()
    }

    /// Whatever held the caret before this dialog opened, so closing hands the
    /// keyboard back instead of stranding focus on a hidden card.
    property Item _restoreFocus: null

    function _ownsFocus() {
        let item = root.Window.activeFocusItem
        while (item) {
            if (item === root)
                return true
            item = item.parent
        }
        return false
    }

    /// Pull the caret onto the card so typing does not leak to the operator
    /// panels underneath. Deferred so a body field (AppPromptDialog) can claim
    /// it first - if one did, leave it there.
    function _ensureFocus() {
        if (root.open && !root._ownsFocus())
            root.forceActiveFocus()
    }

    visible: root.open
    z: Style.modalZ
    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    // The shell is assigned to `data` explicitly: the default property is
    // aliased to the body column, so plain children here would land inside it.
    data: [
        // Escape closes. A window shortcut rather than Keys.onEscapePressed:
        // the card holds no focus of its own unless a body field claims it, so
        // a plain confirm would otherwise never see the key.
        Shortcut {
            sequence: "Escape"
            enabled: root.open && root.escapeEnabled
            context: Qt.WindowShortcut
            onActivated: root.close()
        },

        Connections {
            target: root
            function onOpenChanged() {
                if (root.open) {
                    // Runs before any deferred grab, so this is still the
                    // caller's focus item.
                    root._restoreFocus = root.Window.activeFocusItem
                    Qt.callLater(root._ensureFocus)
                    return
                }
                const back = root._restoreFocus
                root._restoreFocus = null
                if (back)
                    back.forceActiveFocus()
                root.closed()
            }
        },

        ModalScrim {
            anchors.fill: root
            tint: Style.modalScrim
            onClicked: {
                if (root.closeOnScrim)
                    root.close()
            }

            Rectangle {
                anchors.centerIn: parent
                width: Math.max(0, Math.min(parent.width - 80, root.cardWidth))
                height: {
                    const full = cardCol.implicitHeight + 2 * Style.modalPad
                    return root.maxCardHeight > 0
                            ? Math.min(root.maxCardHeight, full) : full
                }
                radius: Style.modalRadius
                color: Style.dialogBg
                border.color: Style.dialogEdge

                // Swallow clicks so the scrim handler does not close the card.
                MouseArea { anchors.fill: parent }

                ColumnLayout {
                    id: cardCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: root.maxCardHeight > 0 ? parent.bottom : undefined
                    anchors.margins: Style.modalPad
                    spacing: Style.modalGap

                    Text {
                        Layout.fillWidth: true
                        visible: root.title.length > 0
                        text: root.title
                        color: Style.dialogTitle
                        font.pixelSize: Style.fontTitle
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: root.blurb.length > 0
                        text: root.blurb
                        color: Style.textMuted
                        font.pixelSize: Style.fontBody
                        // It may quote a file's name or a song's title.
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                    }

                    ColumnLayout {
                        id: bodyCol
                        Layout.fillWidth: true
                        Layout.fillHeight: root.maxCardHeight > 0
                        visible: bodyCol.children.length > 0
                        spacing: 8
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Item { Layout.fillWidth: true }

                        AppButton {
                            label: root.acknowledge ? qsTr("Close") : root.cancelLabel
                            // Sole action on a message box, so it is not ghosted.
                            ghost: !root.acknowledge
                            minWidth: 88
                            onClicked: root.close()
                        }

                        AppButton {
                            visible: !root.acknowledge
                            label: root.acceptLabel
                            danger: root.danger
                            primary: !root.danger
                            enabled: root.acceptEnabled
                            minWidth: 88
                            onClicked: root.submit()
                        }
                    }
                }
            }
        }
    ]
}
