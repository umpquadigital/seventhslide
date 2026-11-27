pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The Sync page's Edit card for a row of the computers list
// (``SettingsSyncState.editAsked``): a computer of the open library, or an
// invite waiting for its computer.
//
// For a computer: its role, and whether a viewer holds the admin key, which an
// admin changes with the password; and the addresses this computer reaches it
// at. Those it gave itself are listed and stay. Those added here are tried
// first, and can be taken away: they go on no list, and its key is still
// checked there.
//
// For an invite: where the computer joining can be found, when it can reach no
// computer of the library itself (a server on the internet, with every computer
// here behind a router). This computer reaches out to it there with the invite
// until it joins, and only a computer holding the invite gets in.
//
// Nothing changes until Save. An address typed and not yet added goes in too.
AppDialog {
    id: card

    required property SettingsSyncState syncState
    /// The row being edited, as it was when the card opened; none before.
    property var row: ({ key: "", name: "", role: "viewer", holdsAdminKey: false, addresses: [], typedAddresses: [] })
    property bool canAdmin: false
    /// What Save sets.
    property string role: "viewer"
    property bool holds: false
    property var typed: []

    readonly property bool invite: !!card.row.invite
    /// The address being typed, read: ``{address}`` or ``{error}``; null while
    /// none is.
    readonly property var pending: addressField.text.trim().length > 0
                                   ? card.syncState.readAddress(card.row, addressField.text, card.typed) : null
    /// Those it gave itself, then those added here.
    readonly property var addressRows: card.row.addresses.map(a => ({ address: a, own: true }))
                                       .concat(card.typed.map(a => ({ address: a, own: false })))

    function edit(row, canAdmin) {
        card.row = row
        card.canAdmin = canAdmin
        card.role = row.role
        // An editor always holds it: made a viewer, it does not unless asked.
        card.holds = row.role === "viewer" && !!row.holdsAdminKey
        card.typed = row.typedAddresses.slice()
        addressField.text = ""
        card.title = card.invite ? row.name : qsTr("Edit “%1”").arg(row.name)
        card.open = true
    }

    /// ``typed``, with the address being typed when it will do.
    function withPending() {
        return card.pending && !card.pending.error ? card.typed.concat([card.pending.address]) : card.typed
    }

    function addPending() {
        if (!card.pending || card.pending.error)
            return
        card.typed = card.withPending()
        addressField.text = ""
    }

    function removeTyped(address) {
        card.typed = card.typed.filter(a => a !== address)
    }

    cardWidth: 480
    danger: false
    acceptLabel: qsTr("Save")
    acceptEnabled: !card.pending || !card.pending.error
    onAccepted: card.syncState.saveEdit(card.row, card.role, card.holds, card.withPending())

    // An invite: what it is for.
    Text {
        Layout.fillWidth: true
        visible: card.invite
        wrapMode: Text.WordWrap
        text: !card.invite ? ""
              : (card.row.role === "viewer" ? qsTr("Joins as a viewer. Made by %1, good until %2.")
                                            : qsTr("Joins as an editor. Made by %1, good until %2."))
                .arg(card.row.madeBy)
                .arg(new Date(card.row.expires).toLocaleString(Qt.locale(), Locale.ShortFormat))
        color: Style.textStrong
        font.pixelSize: Style.fontBody
    }

    // A computer: its role.
    Text {
        visible: !card.invite
        text: qsTr("Role")
        color: Style.textMuted
        font.pixelSize: Style.fontSmall
    }
    SegmentedControl {
        visible: !card.invite
        enabled: card.canAdmin
        opacity: enabled ? 1 : Style.offOpacity
        options: [qsTr("Editor"), qsTr("Viewer")]
        current: card.role === "viewer" ? 1 : 0
        onChosen: function (index) { card.role = index === 1 ? "viewer" : "editor" }
    }
    Text {
        Layout.fillWidth: true
        visible: !card.invite
        wrapMode: Text.WordWrap
        text: !card.canAdmin ? qsTr("Only a computer holding the admin key changes roles.")
              : card.role === "viewer" ? qsTr("Only receives the library and presents from it.")
              : qsTr("Changes anything in the library.")
        color: Style.textHint
        font.pixelSize: Style.fontSmall
    }
    RowLayout {
        Layout.fillWidth: true
        visible: !card.invite && card.role === "viewer"
        spacing: 8
        CheckMark {
            Layout.alignment: Qt.AlignTop
            checked: card.holds
            enabled: card.canAdmin
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text {
                text: qsTr("Holds the admin key")
                color: Style.textStrong
                font.pixelSize: Style.fontBody
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("For admin work there, with the password. Taken away, a copy it has stays until "
                           + "the password changes.")
                color: Style.textHint
                font.pixelSize: Style.fontSmall
            }
        }
        TapHandler {
            enabled: card.canAdmin
            onTapped: card.holds = !card.holds
        }
    }

    // Where it is reached from here.
    Text {
        Layout.topMargin: 6
        text: card.invite ? qsTr("Where its computer is") : qsTr("Addresses")
        color: Style.textMuted
        font.pixelSize: Style.fontSmall
    }
    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: card.invite
              ? qsTr("Only if the computer joining can reach none of this library’s computers. This "
                     + "computer reaches out to it at these with the invite until it joins.")
              : qsTr("Those added here are tried first, by this computer alone: for when the computer’s "
                     + "own have changed, or none of them can be reached from here.")
        color: Style.textHint
        font.pixelSize: Style.fontSmall
    }
    Repeater {
        model: card.addressRows
        delegate: RowLayout {
            id: addressRow
            required property var modelData
            Layout.fillWidth: true
            Layout.preferredHeight: Style.groupRowH
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: addressRow.modelData.address
                color: Style.text
                font.family: Platform.monoFamily
                font.pixelSize: Style.fontBody
                elide: Text.ElideMiddle
            }
            Text {
                visible: addressRow.modelData.own
                text: qsTr("Its own")
                color: Style.textCaption
                font.pixelSize: Style.fontSmall
            }
            IconToolButton {
                visible: !addressRow.modelData.own
                size: "control"
                icon: "close"
                onClicked: card.removeTyped(addressRow.modelData.address)
            }
        }
    }
    Text {
        visible: card.addressRows.length === 0
        text: qsTr("None yet.")
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        DialogField {
            id: addressField
            Layout.fillWidth: true
            placeholder: qsTr("A name or address, with :port if not the usual one")
            onEdited: function (v) { addressField.text = v }
            // Enter adds what is typed; with nothing typed, it saves.
            onSubmitted: card.pending ? card.addPending() : card.submit()
        }
        AppButton {
            size: "control"
            label: qsTr("Add")
            enabled: !!card.pending && !card.pending.error
            onClicked: card.addPending()
        }
    }
    Text {
        Layout.fillWidth: true
        visible: !!card.pending && !!card.pending.error
        wrapMode: Text.WordWrap
        text: card.pending && card.pending.error ? card.pending.error : ""
        color: Style.textWarning
        font.pixelSize: Style.fontSmall
    }

    // Where it was reached, which stays whatever is typed.
    Text {
        Layout.fillWidth: true
        Layout.topMargin: 4
        visible: !card.invite
        wrapMode: Text.WordWrap
        text: card.invite ? ""
              : card.row.connectedAt ? qsTr("Connected now, from %1.").arg(card.row.connectedAt)
              : card.row.lastReachedAt ? qsTr("Last reached at %1.").arg(card.row.lastReachedAt)
              : qsTr("Not reached from this computer yet.")
        color: Style.textCaption
        font.pixelSize: Style.fontSmall
    }
}
