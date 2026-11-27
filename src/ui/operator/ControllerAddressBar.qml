pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The controller's address bar for a live web page: where the page is now,
// after any link followed in it. It sits where the seek bar does for video.
// An address typed or pasted in, and Enter, sends the page there, as a link
// followed there would (Previous comes back); an address refused says why
// under it. Escape, or working anything else, puts back where it is.
ColumnLayout {
    id: addressRow
    /// Untyped, since ControllerWell makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var well // ControllerWell
    spacing: 0
    Layout.fillWidth: true
    visible: addressRow.well.webControls

    /// Where the page is now.
    readonly property string current: addressRow.well.ready
                                      ? addressRow.well.view.webUrl : ""
    /// The page the bar is for: another stack's, or another page live,
    /// drops what was typed for this one.
    readonly property string page: addressRow.well.ready
                                   ? addressRow.well.session.stacks.focusedId + "\n"
                                     + addressRow.well.view.webSource
                                   : ""
    /// Why the address typed was not gone to; empty once it is typed again.
    property string problem: ""

    /// Show where the page is, over anything typed.
    function showCurrent() {
        address.typed = false
        addressRow.problem = ""
        address.text = addressRow.current
    }

    /// Give up the keys and show where the page is.
    function leave() {
        address.focus = false
        addressRow.showCurrent()
    }

    /// Send the page to the address typed. Refused, it stays to be put right.
    /// Nothing typed, Enter only leaves: the page is not loaded again.
    function go() {
        if (!addressRow.well.ready || !address.typed) {
            addressRow.leave()
            return
        }
        const err = addressRow.well.view.webGoTo(address.text)
        addressRow.problem = err
        if (!err.length)
            addressRow.leave()
    }

    onCurrentChanged: {
        if (address.typed)
            return
        // The whole address taken (the first click) stays taken, so what is
        // pasted still replaces it.
        const all = address.length > 0 && address.selectedText.length === address.length
        address.text = addressRow.current
        if (all)
            address.selectAll()
    }
    onPageChanged: addressRow.leave()
    onVisibleChanged: {
        if (!addressRow.visible)
            addressRow.leave()
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 32
        Layout.topMargin: 3
        Layout.bottomMargin: 3
        radius: Style.radiusSmall
        color: address.activeFocus ? Style.fieldFaceFocus : Style.fieldFace
        border.width: 1
        border.color: addressRow.problem.length ? Style.textDanger
                    : address.activeFocus ? Style.focusEdge
                    : (firstClick.containsMouse ? Style.controlEdgeHover : Style.controlEdge)

        Image {
            id: globe
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            source: Icons.url("globe", Style.textCaption)
            sourceSize: Qt.size(16, 16)
        }
        TextInput {
            id: address
            objectName: "webAddress"
            /// Typed into since it last showed where the page is: the page
            /// moving on leaves it as typed.
            property bool typed: false
            anchors.left: globe.right
            anchors.leftMargin: 8
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            selectByMouse: true
            clip: true
            color: Style.text
            selectionColor: Style.focusEdge
            font.pixelSize: Style.fontBody
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            // The start of a long address is what says where it is; the caret
            // is kept in view only while typing.
            autoScroll: address.activeFocus
            Component.onCompleted: address.text = addressRow.current
            onTextEdited: {
                address.typed = true
                addressRow.problem = ""
            }
            onAccepted: addressRow.go()
            onActiveFocusChanged: {
                // Another window taking the keys (to copy an address from)
                // leaves the bar as it is, to come back to.
                if (!address.activeFocus && address.Window.active)
                    addressRow.showCurrent()
            }
            // The slide steps' keys are the bar's while it has the keys:
            // they do nothing here, and do not move the live item.
            Keys.onShortcutOverride: function (event) {
                event.accepted = event.key === Qt.Key_Up || event.key === Qt.Key_Down
                                 || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown
            }
            Keys.onEscapePressed: function (event) {
                addressRow.leave()
                event.accepted = true
            }
        }
        // The first click takes the whole address, as a browser's bar does, so
        // what is typed or pasted replaces it; from then on a press on the text
        // is the text's (the caret, a selection), and one beside it keeps the
        // bar.
        MouseArea {
            id: firstClick
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.IBeamCursor
            onPressed: function (mouse) {
                if (address.activeFocus
                        && address.contains(firstClick.mapToItem(address, mouse.x, mouse.y)))
                    mouse.accepted = false
            }
            onClicked: {
                if (address.activeFocus)
                    return
                address.forceActiveFocus(Qt.MouseFocusReason)
                address.selectAll()
            }
        }
    }

    // Why the address typed was not gone to, until it is typed again.
    Text {
        objectName: "webAddressProblem"
        Layout.fillWidth: true
        Layout.bottomMargin: 3
        visible: addressRow.problem.length > 0
        text: addressRow.problem
        color: Style.textDanger
        font.pixelSize: Style.fontSmall
        wrapMode: Text.Wrap
    }
}
