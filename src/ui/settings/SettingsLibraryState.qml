import QtQuick
import SeventhSlide

// The Settings dialog's Libraries & Profiles page: a column of libraries and
// a column of the open library's profiles, with one set of buttons acting on
// whichever column was used last. Every change persists at once.
//
// The dialog shows what this asks for: ``reported`` errors, confirms for
// ``removeLibraryAsked``, ``cloudFolderAsked`` and ``deleteProfileAsked``, a
// folder chooser for ``attachAsked``, a name prompt for ``nameAsked`` and an
// invite card for ``joinAsked``. On
// ``settingsReplaced`` it reloads its stored fields: they came from a library
// or profile that is no longer the one in use.
QtObject {
    id: state

    property SessionFacade session
    /// This computer's sync; null when it is off.
    property SyncController sync: null
    /// Each synced library's state, by id (``SyncController.libraryStates``).
    readonly property var syncStates: state.sync ? state.sync.libraryStates : ({})
    /// "library" or "profile": the column the buttons act on.
    property string activeColumn: "library"
    property string selectedLibraryId: ""
    property var selectedProfileId: -1
    /// What the name prompt is for, while it is up.
    property string pendingName: ""
    /// The folder the last ``cloudFolderAsked`` was about.
    property string pendingFolder: ""
    /// Joining a synced library takes this computer's sync.
    readonly property bool canJoin: state.sync !== null
    /// The invite code pasted to join a library, and what it is for.
    property string inviteCode: ""
    readonly property var inviteInfo: state.canJoin && state.inviteCode.trim().length
                                      ? state.sync.readInvite(state.inviteCode) : ({})

    readonly property var libraryRows: state.session ? (state.session.libraries.all || []) : []
    readonly property var profileRows: state.session ? (state.session.profiles.all || []) : []
    readonly property string activeLibraryId: state.session ? String(state.session.libraries.activeId) : ""
    readonly property var selectedLibrary: state.rowFor(state.libraryRows, state.selectedLibraryId)
    /// Profiles belong to the library that is open; another library's are not
    /// readable until it is opened.
    readonly property bool showsProfiles: state.selectedLibraryId === state.activeLibraryId

    /// The folder of the library being removed goes too, for good.
    property bool deleteFolder: false
    /// What was typed to confirm that.
    property string deleteTyped: ""
    /// Only a folder that is the library's alone is deleted with it.
    readonly property bool canDeleteFolder: !!state.selectedLibrary && !state.selectedLibrary.missing
                                            && !!state.selectedLibrary.ownsFolder
    readonly property bool canConfirmRemove: !state.deleteFolder
                                             || (!!state.selectedLibrary
                                                 && state.deleteTyped.trim() === String(state.selectedLibrary.name))
    /// What deleting the folder does, for the confirm.
    readonly property string deleteFolderMessage: !state.selectedLibrary ? ""
        : qsTr("This permanently deletes %1 and everything in it: the songs, services, media and "
               + "themes. It cannot be undone.").arg(String(state.selectedLibrary.path))
          + (state.syncStates[state.selectedLibraryId]
             ? qsTr("\n\nThe other computers keep their copies, and this computer stays on their "
                    + "list until an admin removes it.") : "")

    /// A result to show: an error, or "" to clear the last one.
    signal reported(string error)
    /// Removing library ``selectedLibraryId`` needs the operator's word, and
    /// deleting its folder too (``deleteFolder``) its typed name
    /// (``canConfirmRemove``). On yes, call performRemoveLibrary(selectedLibraryId).
    signal removeLibraryAsked(string message)
    /// Deleting profile ``selectedProfileId`` needs the operator's word. On
    /// yes, call performDeleteProfile(selectedProfileId).
    signal deleteProfileAsked(string message)
    /// The operator wants to pick a library folder; pass it to attachLibrary.
    signal attachAsked()
    /// Folder ``pendingFolder`` is synced by a cloud service, which can damage
    /// a library. On yes, call performAttachLibrary(pendingFolder).
    signal cloudFolderAsked(string message)
    /// A name is needed (``title`` says for what, starting from ``initial``);
    /// pass the answer to submitName.
    signal nameAsked(string title, string acceptLabel, string initial)
    /// The library or profile in use changed.
    signal settingsReplaced()
    /// An invite is needed to join a synced library: the card edits
    /// ``inviteCode`` and calls joinLibrary.
    signal joinAsked()

    /// Show ``err``; true when there was none.
    function libraryError(err) {
        state.reported(err)
        return err.length === 0
    }

    /// The row in ``rows`` whose id is ``id``, or null. Ids compare as text:
    /// a library id is a string, a profile id a number.
    function rowFor(rows, id) {
        for (let i = 0; i < rows.length; ++i) {
            if (String(rows[i].id) === String(id))
                return rows[i]
        }
        return null
    }

    function rowName(rows, id) {
        const row = state.rowFor(rows, id)
        return row ? String(row.name) : ""
    }

    /// The page opens fresh: the open library and its active profile picked.
    function reset() {
        state.activeColumn = "library"
        state.selectedLibraryId = state.activeLibraryId
        state.selectedProfileId = state.session ? state.session.profiles.activeId : -1
    }

    // ---- libraries ----

    function chooseLibrary(id) {
        state.activeColumn = "library"
        state.selectedLibraryId = id
        state.reported("")
    }

    function canOpenLibrary() {
        const row = state.selectedLibrary
        return !!row && !row.active && !row.missing
    }

    /// Open the picked library. Live output clears, which is why it takes a
    /// button rather than a click on the row.
    function openSelectedLibrary() {
        if (!state.session || !state.canOpenLibrary())
            return
        if (!state.libraryError(state.session.libraries.switchLibrary(state.selectedLibraryId)))
            return
        // App settings live in the library that just closed.
        state.settingsReplaced()
        state.selectedProfileId = state.session.profiles.activeId
    }

    function askNewLibrary() {
        state.pendingName = "newLibrary"
        state.nameAsked(qsTr("New library"), qsTr("Create"), qsTr("New Library"))
    }

    function openLibraryFolder() {
        state.attachAsked()
    }

    function askJoinLibrary() {
        if (!state.canJoin)
            return
        state.inviteCode = ""
        state.reported("")
        state.joinAsked()
    }

    /// Join the library ``inviteCode`` is for. It arrives in the background;
    /// SettingsSyncState says so when it has started.
    function joinLibrary() {
        if (!state.canJoin)
            return
        if (state.libraryError(state.sync.joinLibrary(state.inviteCode)))
            state.inviteCode = ""
    }

    /// Add the library in folder ``url`` to the list and pick it, asking
    /// first when a cloud service syncs the folder.
    function attachLibrary(url) {
        if (!state.session)
            return
        const service = String(state.session.libraries.cloudServiceFor(url))
        if (service.length) {
            state.pendingFolder = url
            state.reported("")
            state.cloudFolderAsked(qsTr("This folder is synced by %1. %1 can damage a library "
                                        + "that is open while it syncs, and can lose changes "
                                        + "when two computers use it. Keep libraries in a "
                                        + "folder that is not synced.\n\nAdd it anyway?")
                                   .arg(service))
            return
        }
        state.performAttachLibrary(url)
    }

    function performAttachLibrary(url) {
        state.pendingFolder = ""
        if (!state.session)
            return
        const before = state.libraryRows.length
        if (state.libraryError(state.session.libraries.attachLibrary(url, ""))
                && state.libraryRows.length > before)
            state.chooseLibrary(String(state.libraryRows[state.libraryRows.length - 1].id))
    }

    function askRenameLibrary() {
        if (!state.selectedLibrary)
            return
        state.pendingName = "renameLibrary"
        state.nameAsked(qsTr("Rename library"), qsTr("Rename"), String(state.selectedLibrary.name))
    }

    function showLibraryFolder() {
        if (state.session && state.selectedLibraryId.length)
            state.libraryError(state.session.libraries.showFolder(state.selectedLibraryId))
    }

    function beginRemoveLibrary() {
        const row = state.selectedLibrary
        if (!row || row.active)
            return
        state.reported("")
        state.deleteFolder = false
        state.deleteTyped = ""
        state.removeLibraryAsked(qsTr("Remove “%1” from the list? Its files stay on disk.")
                                 .arg(String(row.name)))
    }

    function performRemoveLibrary(id) {
        if (!state.session)
            return
        const libraries = state.session.libraries
        state.libraryError(state.deleteFolder ? libraries.deleteLibrary(id, state.deleteTyped)
                                              : libraries.removeLibrary(id))
        state.deleteFolder = false
        state.deleteTyped = ""
        // Off the list, even if some of a folder could not be deleted: the
        // error stays up.
        if (state.selectedLibraryId === id && !state.selectedLibrary)
            state.selectedLibraryId = state.activeLibraryId
    }

    // ---- profiles ----

    function chooseProfile(id) {
        state.activeColumn = "profile"
        state.selectedProfileId = id
        state.reported("")
    }

    function canUseProfile() {
        return !!state.session && state.session.ready && state.selectedProfileId >= 0
               && state.selectedProfileId !== state.session.profiles.activeId
    }

    function useSelectedProfile() {
        if (!state.canUseProfile())
            return
        if (!state.libraryError(state.session.profiles.activateProfile(state.selectedProfileId)))
            return
        // Theme priority is part of a profile.
        state.settingsReplaced()
    }

    function askNewProfile() {
        state.pendingName = "newProfile"
        state.nameAsked(qsTr("New profile"), qsTr("Create"), qsTr("New Profile"))
    }

    function duplicateSelectedProfile() {
        if (!state.session || state.selectedProfileId < 0)
            return
        const before = state.profileRows.length
        if (state.libraryError(state.session.profiles.duplicateProfile(state.selectedProfileId, ""))
                && state.profileRows.length > before)
            state.chooseProfile(state.profileRows[state.profileRows.length - 1].id)
    }

    function askRenameProfile() {
        if (state.selectedProfileId < 0)
            return
        state.pendingName = "renameProfile"
        state.nameAsked(qsTr("Rename profile"), qsTr("Rename"),
                        state.rowName(state.profileRows, state.selectedProfileId))
    }

    function beginDeleteProfile() {
        if (state.selectedProfileId < 0 || state.profileRows.length < 2)
            return
        state.reported("")
        state.deleteProfileAsked(qsTr("Permanently delete the profile “%1”?")
                                 .arg(state.rowName(state.profileRows, state.selectedProfileId)))
    }

    function performDeleteProfile(id) {
        if (!state.session)
            return
        if (!state.libraryError(state.session.profiles.deleteProfile(id)))
            return
        state.selectedProfileId = state.session.profiles.activeId
        // Deleting the profile in use activates a survivor, which re-themes live.
        state.settingsReplaced()
    }

    // ---- names ----

    /// The name prompt was answered with ``value``.
    function submitName(value) {
        const action = state.pendingName
        state.pendingName = ""
        if (!state.session)
            return
        const name = String(value || "").trim()
        if (action === "newLibrary") {
            const before = state.libraryRows.length
            if (state.libraryError(state.session.libraries.createLibrary(name))
                    && state.libraryRows.length > before)
                state.chooseLibrary(String(state.libraryRows[state.libraryRows.length - 1].id))
        } else if (action === "renameLibrary") {
            state.libraryError(state.session.libraries.renameLibrary(state.selectedLibraryId, name))
        } else if (action === "newProfile") {
            const before = state.profileRows.length
            if (state.libraryError(state.session.profiles.createProfile(name))
                    && state.profileRows.length > before)
                state.chooseProfile(state.profileRows[state.profileRows.length - 1].id)
        } else if (action === "renameProfile") {
            state.libraryError(state.session.profiles.renameProfile(state.selectedProfileId, name))
        }
    }
}
