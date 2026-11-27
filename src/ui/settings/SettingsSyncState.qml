import QtQuick
import SeventhSlide

// The Settings dialog's Sync tab: this computer's name and the pause, the
// open library synced or not, its computers and its admin work. Every
// action is done at once, not with the dialog's Save. The admin password is
// asked for each change and forgotten after it (SyncController); a code to
// pass on or to write down is shown once, by the dialog.
QtObject {
    id: state

    required property SessionFacade session
    /// Null in a build or a test without sync.
    property SyncController sync: null
    readonly property bool available: state.sync !== null
    /// The open library, as the controller says it.
    readonly property var library: state.available ? state.sync.openLibrary : ({})
    readonly property bool synced: !!state.library.synced
    readonly property bool viewer: state.library.role === "viewer"
    readonly property var computers: state.library.computers || []
    /// Admin work takes the locked admin key: an editor's, or a viewer's given one.
    readonly property bool holdsAdminKey: {
        for (let i = 0; i < state.computers.length; ++i) {
            if (state.computers[i].thisComputer)
                return !!state.computers[i].holdsAdminKey
        }
        return false
    }
    readonly property bool canAdmin: state.synced && state.holdsAdminKey && state.library.state !== "removed"
    readonly property bool working: state.available && state.sync.working
    /// Changes are going in or out now, so Sync Now has nothing to add.
    readonly property bool syncing: ["starting", "joining", "downloading", "sending"]
                                    .indexOf(state.library.state) >= 0

    /// The row picked in the list (its ``key``: a computer, or an invite
    /// waiting for its computer), which the Edit and Remove buttons act on.
    property string selectedKey: ""
    readonly property var selectedComputer: {
        for (let i = 0; i < state.computers.length; ++i) {
            if (state.computers[i].key === state.selectedKey)
                return state.computers[i]
        }
        return null
    }
    /// Any row but this computer's, and a removed one's, may be edited: its
    /// addresses need no password, as they go on no list and the key (or the
    /// invite) is still checked there; its role takes the admin.
    readonly property bool canEditSelected: state.synced && !!state.selectedComputer
                                            && !state.selectedComputer.thisComputer
                                            && !state.selectedComputer.removed
    /// The admin can remove the picked computer, or withdraw the picked invite.
    readonly property bool canRemoveSelected: state.canAdmin && state.canEditSelected

    /// The action waiting for the admin password, and what it needs.
    property string pending: ""
    property var pendingArgs: ({})

    signal reported(string error)
    /// Ask for the admin password: ``title`` says for what.
    signal passwordAsked(string title)
    /// Ask for a new password: ``mode`` is "online" (none before it),
    /// "change" (the current one first) or "recover" (the recovery code
    /// first).
    signal newPasswordAsked(string mode)
    /// An image file for this computer's picture is wanted.
    signal pictureFileAsked()
    /// Edit ``row`` (of ``computers``): its role, when ``canAdmin``, and the
    /// addresses it is reached at from here.
    signal editAsked(var row)
    /// Show a code to pass on or to write down; ``qr`` draws it as a QR
    /// code too.
    signal codeShown(string title, string blurb, string code, bool qr)
    /// Tell the operator something went as asked.
    signal noticeShown(string title, string text)
    /// Confirm before ``action`` (a removal, stopping).
    signal confirmAsked(string title, string text, string action)

    function reset() {
        state.pending = ""
        state.pendingArgs = ({})
        state.selectedKey = ""
    }

    function report(err) {
        if (err.length)
            state.reported(err)
    }

    function setName(name) {
        if (state.available)
            state.report(state.sync.setComputerName(name))
    }

    function setPicture(glyph, colour) {
        if (state.available)
            state.report(state.sync.setComputerPicture(glyph, colour))
    }

    function askPictureFile() { state.pictureFileAsked() }

    function setPictureFile(url) {
        if (state.available)
            state.report(state.sync.setComputerPictureFile(url))
    }

    function setPause(pause) {
        if (state.available)
            state.report(state.sync.setPauseDuringServices(pause))
    }

    function setReachAddresses(addresses) {
        if (state.available)
            state.report(state.sync.setReachAddresses(addresses))
    }

    function setChosenPort(port) {
        if (state.available)
            state.report(state.sync.setChosenPort(port))
    }

    function setShareAddress(share) {
        if (state.available)
            state.report(state.sync.setShareAddress(share))
    }

    function setUploadCap(megabits) {
        if (state.available)
            state.report(state.sync.setUploadCap(megabits))
    }

    function editSelected() {
        if (state.canEditSelected && !state.working)
            state.editAsked(state.selectedComputer)
    }

    /// ``typed`` as one more address for ``row``, beside those ``added``
    /// already: ``{address}`` or ``{error}``.
    function readAddress(row, typed, added) {
        return state.sync.readAddress(typed, row.addresses, added)
    }

    /// From the Edit card: ``addresses`` typed for ``row``, when they differ
    /// from those it had, are set at once, on this computer (setting them
    /// starts its connecting over); a role or admin key that differs then asks
    /// the password.
    function saveEdit(row, role, holds, addresses) {
        const same = addresses.length === row.typedAddresses.length
                     && addresses.every((a, i) => a === row.typedAddresses[i])
        const err = same ? ""
                  : row.invite ? state.sync.setInviteAddresses(row.invite, addresses)
                  : state.sync.setComputerAddresses(row.machine, addresses)
        if (err.length) {
            state.reported(err)
            return
        }
        if (row.invite || !state.canAdmin)
            return
        const viewer = role === "viewer"
        if (role !== row.role || (viewer && holds !== !!row.holdsAdminKey)) {
            state.withPassword("role", { machine: row.machine, role: role, holds: viewer && holds },
                               role !== row.role ? (viewer ? qsTr("Make a viewer") : qsTr("Make an editor"))
                               : holds ? qsTr("Give the admin key") : qsTr("Stop sending the admin key"))
        }
    }

    function setAutomatic(automatically) {
        if (state.available)
            state.report(state.sync.setSyncAutomatically(automatically))
    }

    /// Ask the password, then do ``action`` with ``args``.
    function withPassword(action, args, title) {
        state.pending = action
        state.pendingArgs = args
        state.passwordAsked(title)
    }

    function submitPassword(password) {
        const a = state.pendingArgs
        let err = ""
        switch (state.pending) {
        case "invite": err = state.sync.admin.invite(a.role, 7, password); break
        case "withdraw": err = state.sync.admin.withdrawInvite(a.invite, password); break
        case "role": err = state.sync.admin.setRole(a.machine, a.role, a.holds, password); break
        case "remove": err = state.sync.admin.removeComputer(a.machine, password); break
        case "limit": err = state.sync.admin.setSizeLimit(a.gigabytes, password); break
        case "recovery": err = state.sync.admin.newRecoveryCode(password); break
        }
        // A computer removed, or an invite withdrawn, is no longer one to pick;
        // one whose role changed still is, to edit again.
        if (state.pending === "remove" || state.pending === "withdraw")
            state.selectedKey = ""
        state.pending = ""
        state.pendingArgs = ({})
        state.report(err)
    }

    /// From the new-password card: ``first`` is the current password or the
    /// recovery code (empty when putting the library online).
    function submitNewPassword(mode, first, fresh) {
        const err = mode === "online" ? state.sync.putOnline(fresh)
                  : state.sync.admin.changePassword(first, fresh, mode === "recover")
        state.report(err)
    }

    function passphraseProblem(text) {
        return state.available ? state.sync.passphraseProblem(text) : ""
    }

    function putOnline() { state.newPasswordAsked("online") }
    function changePassword() { state.newPasswordAsked("change") }
    function recoverPassword() { state.newPasswordAsked("recover") }
    function newRecoveryCode() { state.withPassword("recovery", {}, qsTr("New recovery code")) }
    function invite(role) {
        state.withPassword("invite", { role: role },
                           role === "viewer" ? qsTr("Invite a viewer") : qsTr("Invite an editor"))
    }
    function removeSelected() {
        if (!state.canRemoveSelected)
            return
        if (state.selectedComputer.invite)
            state.askWithdraw(state.selectedComputer)
        else
            state.askRemove(state.selectedComputer)
    }
    function askWithdraw(row) {
        state.pendingArgs = { invite: row.invite }
        state.confirmAsked(qsTr("Withdraw invite"),
                           qsTr("Withdraw this invite? No computer can join by it from now on, wherever "
                                + "its code was given. A computer that joined by it while this one "
                                + "could not hear does not count either."),
                           "withdraw")
    }
    function askRemove(computer) {
        state.pendingArgs = { machine: computer.machine }
        state.confirmAsked(qsTr("Remove computer"),
                           qsTr("Remove “%1” from the library for good? What it already sent stays. "
                                + "It can come back only with a new invite after SeventhSlide is "
                                + "installed on it again.").arg(computer.name),
                           "remove")
    }
    function setLimit(gigabytes) {
        state.withPassword("limit", { gigabytes: gigabytes }, qsTr("Size limit"))
    }
    function askStopSyncing() {
        state.confirmAsked(qsTr("Stop syncing"),
                           qsTr("Stop syncing “%1” on this computer? It stays here as a library of its "
                                + "own, and this computer leaves the library’s list of computers.")
                           .arg(state.library.name || ""),
                           "stop")
    }
    function syncNow() {
        if (state.available)
            state.sync.syncNow()
    }

    /// A confirm was accepted.
    function confirmed(action) {
        if (action === "remove") {
            state.withPassword("remove", state.pendingArgs, qsTr("Remove computer"))
        } else if (action === "withdraw") {
            state.withPassword("withdraw", state.pendingArgs, qsTr("Withdraw invite"))
        } else if (action === "stop") {
            state.report(state.sync.stopSyncing(state.session.libraries.activeId))
        }
    }

    /// What came of an action the controller ran.
    function finished(action, result) {
        if (!result.ok) {
            state.reported(result.error || qsTr("That could not be done."))
            return
        }
        switch (action) {
        case "putOnline":
            state.codeShown(qsTr("Write down the recovery code"),
                            qsTr("The library is online. If the admin password is ever forgotten, "
                                 + "this code sets a new one. Write it down and keep it somewhere "
                                 + "safe: it is shown only now."),
                            result.recoveryCode, false)
            break
        case "invite":
            state.codeShown(qsTr("Invite"),
                            qsTr("On the computer joining: Settings › Libraries & Profiles › New… › Join a "
                                 + "Synced Library, and paste this code. It works once, for 7 days. Until "
                                 + "then it waits among the computers, where Edit can say where its "
                                 + "computer is, if that one can reach none of these."),
                            result.code, true)
            break
        case "password":
            if (result.recoveryCode)
                state.codeShown(qsTr("Write down the new recovery code"),
                                qsTr("The admin password is changed. The recovery code was used, so "
                                     + "here is a new one; the old one no longer works."),
                                result.recoveryCode, false)
            else
                state.noticeShown(qsTr("Password changed"),
                                  qsTr("The admin password is changed on every computer of the library."))
            break
        case "recovery":
            state.codeShown(qsTr("Write down the recovery code"),
                            qsTr("The old recovery code no longer works. Write this one down and keep it "
                                 + "somewhere safe: it is shown only now."),
                            result.recoveryCode, false)
            break
        case "stopSyncing":
            if (!result.heard)
                state.noticeShown(qsTr("Stopped syncing"),
                                  qsTr("No other computer of the library was there to hear that this one "
                                       + "left, so they may still list it. An admin can remove it from "
                                       + "the list on the Sync page of any editor computer."))
            break
        case "join":
            state.noticeShown(qsTr("Joining"),
                              qsTr("The library is coming. It opens here once all of it has arrived; "
                                   + "its row in the list of libraries shows how it is getting on."))
            break
        }
    }

    property Connections _results: Connections {
        target: state.sync
        function onFinished(action, result) { state.finished(action, result) }
    }
}
