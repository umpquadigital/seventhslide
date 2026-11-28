pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import SeventhSlide

// The Sync page's Edit window for a row of the computers list
// (``SettingsSyncState.editAsked``): a computer of the open library, or an
// invite waiting for its computer. A WindowShell over Settings, without a
// rail: the row at the top, then its sections.
//
// For a computer: its role, and whether a viewer holds the admin key, which an
// admin changes with the password; and the addresses this computer reaches it
// at, in a well with a toolbar over it. Those it was learned at (the ones it
// gives for itself, which sync keeps up to date) are listed and stay. Those
// added here (Add…, a prompt) are tried first, and can be picked and taken
// away: they go on no list, and its key is still checked there.
//
// For an invite: where the computer joining can be found, when it can reach no
// computer of the library itself (a server on the internet, with every computer
// here behind a router). This computer reaches out to it there with the invite
// until it joins, and only a computer holding the invite gets in.
//
// Nothing changes until Save: closing it otherwise (Cancel, Escape, a click
// outside) asks first when something was changed.
//
// A focus scope that takes the keys while open, so Escape reaches it, and
// Delete (or Backspace) takes the picked address away.
FocusScope {
    id: card

    required property SettingsSyncState syncState
    property bool open: false
    /// The row being edited, as it was when the window opened; none before.
    property var row: ({ key: "", name: "", role: "viewer", holdsAdminKey: false, addresses: [], typedAddresses: [] })
    property bool canAdmin: false
    /// What Save sets.
    property string role: "viewer"
    property bool holds: false
    property var typed: []
    /// Where the keys go back to on closing when what held them is gone (a
    /// row of the computers list, made again as sync changed it meanwhile).
    property Item returnFocusTo: null

    /// The address row picked in the well, which the toolbar's trash takes away.
    property string selectedAddress: ""

    readonly property bool invite: !!card.row.invite
    /// Those it was learned at, then those added here.
    readonly property var addressRows: card.row.addresses.map(a => ({ address: a, learned: true }))
                                       .concat(card.typed.map(a => ({ address: a, learned: false })))
    /// Under its name: when it joined (founded the library, for the first
    /// computer); an invite, when it expires.
    /// Nothing when its entry does not say when, or says something that is
    /// not a time (written by another computer).
    readonly property string subtitle: {
        if (card.invite) {
            const expires = card.syncState.dateTimeText(card.row.expires)
            return expires ? qsTr("Invitation expires %1").arg(expires) : ""
        }
        const joined = card.syncState.dateText(card.row.joined)
        return !joined ? ""
             : card.row.founded ? qsTr("Founded this library on %1").arg(joined)
             : qsTr("Joined this library on %1").arg(joined)
    }
    /// The Address column's neighbour: where each address came from.
    readonly property int sourceW: 150
    /// The picked address can be taken away: one added here, not one learned.
    readonly property bool canRemoveSelected: card.typed.indexOf(card.selectedAddress) >= 0

    /// Whether it held the admin key as a viewer when the window opened. An
    /// editor always holds it: made a viewer, it does not unless asked.
    readonly property bool heldBefore: card.row.role === "viewer" && !!card.row.holdsAdminKey

    function edit(row, canAdmin) {
        card.row = row
        card.canAdmin = canAdmin
        card.role = row.role
        card.holds = card.heldBefore
        card.typed = row.typedAddresses.slice()
        card.selectedAddress = ""
        page.contentY = 0
        card._restoreFocus = card.Window.activeFocusItem
        card.open = true
        card.forceActiveFocus()
    }

    function close() {
        discardConfirm.open = false
        card.open = false
    }

    /// Whether closing now would lose anything Save would set. The admin key
    /// counts only for a viewer: an editor holds it whatever the switch said.
    function hasChanges() {
        return card.open && (card.role !== card.row.role
                             || (card.role === "viewer" && card.holds !== card.heldBefore)
                             || JSON.stringify(card.typed) !== JSON.stringify(card.row.typedAddresses))
    }

    /// Close as the operator asks for it (Cancel, Escape, a click outside):
    /// unsaved changes are not lost unasked.
    function requestCancel() {
        if (card.hasChanges())
            discardConfirm.open = true
        else
            card.close()
    }

    /// Whatever held the keys before the window opened (in Settings), so
    /// closing hands them back; ``returnFocusTo`` once it is gone.
    property Item _restoreFocus: null
    onOpenChanged: {
        if (card.open)
            return
        const back = card._restoreFocus || card.returnFocusTo
        card._restoreFocus = null
        if (back)
            back.forceActiveFocus()
    }

    /// Save: what the window holds. The window closes first, so a password
    /// asked for opens over Settings.
    function submit() {
        card.open = false
        card.syncState.saveEdit(card.row, card.role, card.holds, card.typed)
    }

    /// Ask for an address to add, the prompt holding ``initial``.
    function askAddress(initial) {
        addressPrompt.ask(initial)
    }

    /// Add ``read`` (``{address}``, as the prompt read it), and pick it.
    function addAddress(read) {
        if (!read || read.error)
            return
        card.typed = card.typed.concat([read.address])
        card.selectedAddress = read.address
    }

    function removeSelected() {
        if (!card.canRemoveSelected)
            return
        card.typed = card.typed.filter(a => a !== card.selectedAddress)
        card.selectedAddress = ""
    }

    visible: card.open
    focus: card.open
    // Over Settings, which opens it from its Sync page.
    z: Style.sheetZ + 1
    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    Keys.onEscapePressed: function (event) {
        card.requestCancel()
        event.accepted = true
    }
    // Delete, or Backspace (a Mac's Delete key), takes the picked address away.
    Keys.onPressed: function (event) {
        if ((event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) && card.canRemoveSelected) {
            card.removeSelected()
            event.accepted = true
        }
    }

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: card.requestCancel()
    }

    WindowShell {
        id: shell
        anchors.centerIn: parent
        width: Math.min(Style.computerWindowW, parent.width - 40)
        // As tall as its sections, up to the window's room; then they scroll.
        height: Math.min(page.contentHeight + 2 * Style.windowBarH + 2, parent.height - 40)
        title: card.invite ? qsTr("Edit Invite") : qsTr("Edit Computer")
        acceptLabel: qsTr("Save")
        note: card.invite || card.canAdmin ? "" : qsTr("Only a computer holding the admin key changes roles.")
        onCanceled: card.requestCancel()
        onAccepted: card.submit()

        ScrollPage {
            id: page
            anchors.fill: parent
            // The window grows and shrinks with its sections: once they fit
            // again, the scrollbar goes.
            stickyGutter: false

            // The row: its picture, its name and when it joined (an invite:
            // when it expires).
            GroupList {
                width: page.columnWidth

                GroupRow {
                    implicitHeight: 60
                    Avatar {
                        size: 34
                        // An invite's computer is not known yet: no picture of its own.
                        picture: card.invite ? ({ glyph: "person", colour: Style.textCaption }) : card.row.picture
                        name: card.row.name
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: card.row.name
                            color: Style.textBright
                            font.pixelSize: Style.fontUi
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: card.subtitle.length > 0
                            text: card.subtitle
                            color: Style.textCaption
                            font.pixelSize: Style.fontSmall
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            // ---------------------------------------------------------- Role
            GroupHeading {
                visible: !card.invite
                text: qsTr("Role")
            }
            GroupList {
                visible: !card.invite
                width: page.columnWidth

                GroupRow {
                    FormRow {
                        Layout.fillWidth: true
                        label: qsTr("Role")
                        SegmentedControl {
                            enabled: card.canAdmin
                            opacity: enabled ? 1 : Style.offOpacity
                            options: [qsTr("Editor"), qsTr("Viewer")]
                            current: card.role === "viewer" ? 1 : 0
                            onChosen: function (index) { card.role = index === 1 ? "viewer" : "editor" }
                        }
                    }
                }
                // An editor always holds it.
                GroupRow {
                    visible: card.role === "viewer"
                    FormRow {
                        Layout.fillWidth: true
                        label: qsTr("Holds the admin key")
                        ToggleSwitch {
                            enabled: card.canAdmin
                            opacity: enabled ? 1 : Style.offOpacity
                            checked: card.holds
                            onToggled: card.holds = !card.holds
                        }
                    }
                }
            }

            // -------------------------------------------------------- Routes
            GroupHeading {
                text: qsTr("Routes")
                tools: [
                    AppButton {
                        size: "control"
                        iconSource: Icons.url("trash", ink)
                        enabled: card.canRemoveSelected
                        onClicked: card.removeSelected()
                    },
                    AppButton {
                        size: "control"
                        primary: true
                        label: qsTr("Add…")
                        iconSource: Icons.url("plus", ink)
                        onClicked: card.askAddress("")
                    }
                ]
            }
            // Each address and where it came from: those it was learned at
            // stay; one added here, picked, the toolbar takes away.
            ListWell {
                width: page.columnWidth
                height: addressPane.implicitHeight + 2

                WellPane {
                    id: addressPane
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    showHeader: false

                    TableHeaderBar {
                        TableColumnHead {
                            Layout.fillWidth: true
                            label: qsTr("Address")
                        }
                        TableGroove {}
                        TableColumnHead {
                            Layout.preferredWidth: card.sourceW + 6
                            label: qsTr("Source")
                        }
                    }

                    // Inset and apart, as a well's list rows are.
                    Column {
                        id: addressList
                        Layout.fillWidth: true
                        padding: 6
                        spacing: 3

                        Repeater {
                            model: card.addressRows
                            delegate: TableRow {
                                id: addressRow
                                required property var modelData
                                width: addressList.width - addressList.leftPadding - addressList.rightPadding
                                implicitHeight: Style.listRowH
                                selected: card.selectedAddress === addressRow.modelData.address
                                onClicked: card.selectedAddress = addressRow.modelData.address

                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Image {
                                        id: addressIcon
                                        anchors.left: parent.left
                                        anchors.leftMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 14
                                        height: 14
                                        source: Icons.url("globe", addressRow.modelData.learned ? Style.textCaption
                                                                                                : Style.textSoft, 2)
                                        sourceSize: Qt.size(14, 14)
                                    }
                                    Text {
                                        anchors.left: addressIcon.right
                                        anchors.right: parent.right
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: addressRow.modelData.address
                                        color: addressRow.modelData.learned ? Style.textSoft : Style.textBright
                                        font.family: Platform.monoFamily
                                        font.pixelSize: Style.fontBody
                                        elide: Text.ElideMiddle
                                    }
                                }
                                TableGroove {}
                                Item {
                                    Layout.preferredWidth: card.sourceW
                                    Layout.fillHeight: true
                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: 10
                                        anchors.right: parent.right
                                        anchors.rightMargin: 10
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: addressRow.modelData.learned ? qsTr("Learned") : qsTr("Added here")
                                        color: addressRow.modelData.learned ? Style.textCaption : Style.textSoft
                                        font.pixelSize: Style.fontBody
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                        Text {
                            visible: card.addressRows.length === 0
                            width: addressList.width - addressList.leftPadding - addressList.rightPadding
                            topPadding: 10
                            bottomPadding: 10
                            horizontalAlignment: Text.AlignHCenter
                            text: card.invite ? qsTr("No addresses added.") : qsTr("No addresses yet.")
                            color: Style.textCaption
                            font.pixelSize: Style.fontBody
                        }
                    }
                }
            }
        }
    }

    // An address to add: read as it is typed, and refused with why.
    AppPromptDialog {
        id: addressPrompt
        /// What is typed, read: ``{address}`` or ``{error}``; null while
        /// nothing is. Not tied to ``open``: Add reads it once the prompt closes.
        readonly property var read: addressPrompt.trimmedValue.length > 0
                                    ? card.syncState.readAddress(card.row, addressPrompt.value, card.typed) : null
        title: qsTr("Add an address")
        blurb: card.invite ? qsTr("This computer looks for the new computer at these addresses until it joins.")
                           : qsTr("This computer tries these addresses first when it connects to %1.")
                             .arg(card.row.name)
        placeholder: qsTr("A name or address, with :port if not the usual one")
        autoSize: false
        cardWidth: Style.modalWidth + 60
        acceptLabel: qsTr("Add")
        acceptEnabled: !!addressPrompt.read && !addressPrompt.read.error
        onAccepted: card.addAddress(addressPrompt.read)

        Text {
            Layout.fillWidth: true
            visible: !!addressPrompt.read && !!addressPrompt.read.error
            wrapMode: Text.WordWrap
            text: addressPrompt.read && addressPrompt.read.error ? addressPrompt.read.error : ""
            // What was typed, quoted back: never read as markup.
            textFormat: Text.PlainText
            color: Style.textWarning
            font.pixelSize: Style.fontSmall
        }
    }

    DiscardConfirm {
        id: discardConfirm
        objectName: "computerDiscardConfirm"
        onOpenChanged: if (!discardConfirm.open && card.open) card.forceActiveFocus()
        blurb: qsTr("Closing the window now loses what you have not saved.")
        onAccepted: card.close()
    }
}
