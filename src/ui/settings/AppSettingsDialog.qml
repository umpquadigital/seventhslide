pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import SeventhSlide

// App-wide settings: CCLI, theme priority, Library tabs, libraries and
// profiles, sync, stacks, the web remote, Bibles, fonts and the media tools, as
// pages in a WindowShell.
//
// Each tab's state and actions are its view-model (Settings*State); this file
// is the sheet around them: the tabs, Save / Cancel for the fields that wait
// for Save (General, Web Remote), and the pieces a view-model asks for that
// draw above it - confirms, file choosers, drag ghosts.
// A focus scope that takes the keys while open, so Escape reaches it, and
// never holds them for the window behind it while closed.
FocusScope {
    id: root
    visible: open
    focus: open
    z: Style.sheetZ

    required property SessionFacade session
    /// The web remote's live state (running, port, devices). Null when the
    /// dialog is shown without one.
    property RemoteController remote: null
    /// This computer's sync. Null when it is off.
    property SyncController sync: null

    property bool open: false
    property string errorText: ""
    /// Output ``name``'s settings were asked for from the Outputs page; the
    /// Output window opens over this one.
    signal outputSettingsRequested(string name)
    /// A song was asked for from the Fonts page: library song ``id``, or with
    /// ``item`` the song of service row ``id``. The song editor opens over this
    /// one.
    signal songEditRequested(int id, bool item)
    property string tab: "general" // general | library | sync | outputs | stacks | remote | bibles | fonts | tools

    SettingsGeneralState {
        id: generalState
        session: root.session
    }

    SettingsLibraryState {
        id: libraryState
        session: root.session
        sync: root.sync
        onReported: function (error) { root.errorText = error }
        onRemoveLibraryAsked: function (message) {
            removeLibraryConfirm.asked = message
            removeLibraryConfirm.open = true
        }
        onCloudFolderAsked: function (message) { cloudFolderConfirm.ask(message) }
        onDeleteProfileAsked: function (message) { deleteProfileConfirm.ask(message) }
        onAttachAsked: libraryFolderDialog.open()
        onJoinAsked: joinDialog.open = true
        onNameAsked: function (title, acceptLabel, initial) {
            libraryNamePrompt.title = title
            libraryNamePrompt.acceptLabel = acceptLabel
            libraryNamePrompt.ask(initial)
        }
        // Re-read the stored fields so Save cannot write the previous
        // library's (or profile's) values back.
        onSettingsReplaced: root.load()
    }

    SettingsSyncState {
        id: syncState
        session: root.session
        sync: root.sync
        onReported: function (error) { root.errorText = error }
        onPasswordAsked: function (title) {
            syncPasswordPrompt.title = title
            syncPasswordPrompt.ask("")
        }
        onNewPasswordAsked: function (mode) { newPasswordDialog.askFor(mode) }
        onPictureFileAsked: pictureFileDialog.open()
        onEditAsked: function (row) { computerEdit.edit(row, syncState.canAdmin) }
        onCodeShown: function (title, blurb, code, qr) { codeDialog.show(title, blurb, code, qr) }
        onNoticeShown: function (title, text) {
            syncNotice.title = title
            syncNotice.ask(text)
        }
        onConfirmAsked: function (title, text, action) {
            syncConfirm.title = title
            syncConfirm.action = action
            syncConfirm.acceptLabel = action === "stop" ? qsTr("Stop Syncing") : qsTr("Remove")
            syncConfirm.ask(text)
        }
    }

    SettingsOutputsState {
        id: outputsState
        session: root.session
        onReported: function (error) { root.errorText = error }
        onOutputSettingsRequested: function (name) { root.outputSettingsRequested(name) }
        onDeleteAsked: function (name, message) {
            deleteOutputConfirm.outputName = name
            deleteOutputConfirm.ask(message)
        }
    }

    SettingsStacksState {
        id: stackState
        session: root.session
        onReported: function (error) { root.errorText = error }
        onDeleteAsked: function (message) { deleteStackConfirm.ask(message) }
        onPreviewOffAsked: function (message) { previewOffConfirm.ask(message) }
    }

    SettingsRemoteState {
        id: remoteState
        session: root.session
        remote: root.remote
    }

    SettingsBiblesState {
        id: bibleState
        session: root.session
        onReported: function (error) { root.errorText = error }
        onDeleteAsked: function (message) { deleteBibleConfirm.ask(message) }
        onImportAsked: bibleDialog.open()
    }

    SettingsFontsState {
        id: fontState
        session: root.session
        active: root.open && root.tab === "fonts"
        onReported: function (error) { root.errorText = error }
        onRemoveAsked: function (message) { removeFontConfirm.ask(message) }
        onAddAsked: fontFilesDialog.open()
        onResultsShown: function (results, note) { fontResults.show(results, note) }
        onOutputAsked: function (name) { root.outputSettingsRequested(name) }
        onSongAsked: function (id, item) { root.songEditRequested(id, item) }
        onProfileAsked: function (id) {
            root.tab = "library"
            libraryState.chooseProfile(id)
        }
    }

    onTabChanged: {
        fontState.reset()
        bibleState.reset()
        libraryState.reset()
        syncState.reset()
    }

    function openDialog() {
        root.openPage("general")
    }

    /// Open on page ``page`` (one of ``tab``'s): the bottom bar's library
    /// button opens Libraries & Profiles.
    function openPage(page) {
        if (!root.session)
            return
        root.errorText = ""
        root.tab = page
        root.load()
        root.open = true
        root.forceActiveFocus()
    }

    /// A window opened over this one has closed: what it changed may change
    /// what the pages show.
    function editorClosed() {
        fontState.reread()
    }

    function close() {
        root.open = false
        root.errorText = ""
        fontState.reset()
        bibleState.reset()
        libraryState.reset()
    }

    /// The settings as last read: Save sends only what differs from them,
    /// so a change another computer made meanwhile (sync) is not put back.
    property var loadedSettings: ({})

    function load() {
        const d = root.session.getAppSettings()
        root.loadedSettings = d
        generalState.load(d)
        remoteState.load(d)
        stackState.reset()
    }

    /// The fields that wait for Save, those that differ from what was read.
    function changedSettings() {
        const fields = {
            organizationName: generalState.organizationName,
            ccliLicenceNumber: generalState.ccli,
            themePriority: generalState.priority,
            remoteEnabled: remoteState.remoteOn,
            remotePin: remoteState.remotePin,
            remotePort: remoteState.remotePort
        }
        const changed = {}
        for (const key in fields) {
            if (JSON.stringify(fields[key]) !== JSON.stringify(root.loadedSettings[key]))
                changed[key] = fields[key]
        }
        return changed
    }

    /// Whether closing now would lose anything: the other pages keep each
    /// change as it is made.
    function hasChanges() {
        return root.open && (Object.keys(root.changedSettings()).length > 0
                             || generalState.tabsChanged())
    }

    /// Close as the operator asks for it: unsaved changes are not lost unasked.
    function requestCancel() {
        if (root.hasChanges())
            discardConfirm.open = true
        else
            root.close()
    }

    function save() {
        if (!root.session)
            return
        const err = root.session.updateAppSettings(root.changedSettings())
        if (err.length) {
            root.errorText = err
            return
        }
        generalState.saveTabs()
        root.close()
    }

    /// Place ``ghost`` under the pointer, at (x, y) in ``item``, held at
    /// (grabX, grabY).
    function placeGhost(ghost, item, x, y, grabX, grabY) {
        const p = item.mapToItem(ghost.parent, x, y)
        ghost.x = p.x - grabX
        ghost.y = p.y - grabY
    }

    Connections {
        target: generalState.tabDrag
        function onGhostMoved(item, x, y) {
            root.placeGhost(tabGhost, item, x, y, generalState.tabDrag.grabX,
                            generalState.tabDrag.grabY)
        }
    }

    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    // Same gesture as the other editor shells (Output, Freeform, Song): Escape
    // leaves, doing exactly what Cancel and the scrim already do. Inline renames
    // accept Escape themselves, so this only fires when none is open.
    Keys.onEscapePressed: function (event) {
        root.requestCancel()
        event.accepted = true
    }

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: root.requestCancel()
    }

    WindowShell {
        id: modalBox
        anchors.centerIn: parent
        width: Math.min(Style.settingsWindowW, parent.width - 40)
        height: Math.min(Style.settingsWindowH, parent.height - 40)
        title: qsTr("Settings")
        pages: [
            { id: "general", label: qsTr("General"), icon: "settings" },
            { id: "library", label: qsTr("Libraries & Profiles"), icon: "database" },
            { id: "sync", label: qsTr("Sync"), icon: "sync" },
            { id: "outputs", label: qsTr("Outputs"), icon: "display" },
            { id: "stacks", label: qsTr("Stacks"), icon: "layers" },
            { id: "remote", label: qsTr("Web Remote"), icon: "phone" },
            { id: "bibles", label: qsTr("Bibles"), icon: "bible" },
            { id: "fonts", label: qsTr("Fonts"), icon: "textBox" },
            { id: "tools", label: qsTr("Updates & Tools"), icon: "hammer" }
        ]
        page: root.tab
        onPageActivated: function (id) { root.tab = id }
        errorText: root.errorText
        onCanceled: root.requestCancel()
        onAccepted: root.save()

        // Each page is made the first time it shows, and kept.
        SettingsPage {
            shown: root.open && root.tab === "general"
            objectName: "SettingsGeneralPanel"
            sourceComponent: SettingsGeneralPanel {
                anchors.fill: parent
                dialog: generalState
            }
        }
        SettingsPage {
            shown: root.open && root.tab === "library"
            objectName: "SettingsLibraryPanel"
            sourceComponent: SettingsLibraryPanel {
                anchors.fill: parent
                dialog: libraryState
            }
        }
        SettingsPage {
            shown: root.open && root.tab === "sync"
            objectName: "SettingsSyncPanel"
            sourceComponent: SettingsSyncPanel {
                anchors.fill: parent
                dialog: syncState
            }
        }
        SettingsPage {
            shown: root.open && root.tab === "outputs"
            objectName: "SettingsOutputsPanel"
            sourceComponent: SettingsOutputsPanel {
                anchors.fill: parent
                dialog: outputsState
            }
        }
        SettingsPage {
            shown: root.open && root.tab === "stacks"
            objectName: "SettingsStacksPanel"
            sourceComponent: SettingsStacksPanel {
                anchors.fill: parent
                dialog: stackState
            }
        }
        SettingsPage {
            shown: root.open && root.tab === "remote"
            objectName: "SettingsRemotePanel"
            sourceComponent: SettingsRemotePanel {
                anchors.fill: parent
                dialog: remoteState
            }
        }
        SettingsPage {
            shown: root.open && root.tab === "bibles"
            objectName: "SettingsBiblesPanel"
            sourceComponent: SettingsBiblesPanel {
                anchors.fill: parent
                dialog: bibleState
            }
        }
        SettingsPage {
            shown: root.open && root.tab === "fonts"
            objectName: "SettingsFontsPanel"
            sourceComponent: SettingsFontsPanel {
                anchors.fill: parent
                dialog: fontState
            }
        }
        SettingsPage {
            shown: root.open && root.tab === "tools"
            objectName: "SettingsToolsPanel"
            sourceComponent: SettingsToolsPanel {
                anchors.fill: parent
                session: root.session
            }
        }

        // The row being dragged, following the pointer; the row itself fades.
        DragGhost {
            id: tabGhost
            width: Math.min(modalBox.body.width, 420)
            visible: generalState.tabDrag.active
            iconSource: generalState.tabDrag.icon
            label: generalState.tabDrag.label
        }
    }

    DiscardConfirm {
        id: discardConfirm
        objectName: "settingsDiscardConfirm"
        onOpenChanged: if (!open && root.open) root.forceActiveFocus()
        blurb: qsTr("Closing Settings now loses what you have not saved.")
        onAccepted: root.close()
    }

    AppDialog {
        id: deleteOutputConfirm
        property string outputName: ""
        title: qsTr("Delete output")
        acceptLabel: qsTr("Delete")
        onAccepted: outputsState.performDeleteOutput(deleteOutputConfirm.outputName)
    }

    AppDialog {
        id: deleteBibleConfirm
        title: qsTr("Delete translation")
        acceptLabel: qsTr("Delete")
        onAccepted: bibleState.performDeleteBible(bibleState.selectedBibleId)
    }

    AppDialog {
        id: removeFontConfirm
        title: qsTr("Remove font")
        acceptLabel: qsTr("Remove")
        onAccepted: fontState.performRemove()
    }

    // What each file added came to.
    AppDialog {
        id: fontResults
        property var results: []
        property string note: ""
        function show(results, note) {
            fontResults.results = results
            fontResults.note = note
            fontResults.open = true
        }
        title: qsTr("Add fonts")
        acknowledge: true
        cardWidth: 520
        onClosed: fontResults.results = []

        ListView {
            id: fontResultList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(fontResultList.contentHeight, 300)
            spacing: 8
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: fontResults.results
            delegate: Column {
                id: fontResult
                required property var modelData
                width: ListView.view.width
                spacing: 1
                Text {
                    width: parent.width
                    text: fontResult.modelData.file
                    color: Style.text
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: fontResult.modelData.text
                    color: fontResult.modelData.ok ? Style.textSoft : Style.textWarning
                    font.pixelSize: Style.fontSmall
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: fontResults.note.length > 0
            wrapMode: Text.WordWrap
            text: fontResults.note
            color: Style.textWarning
            font.pixelSize: Style.fontSmall
        }
    }

    AppDialog {
        id: deleteStackConfirm
        title: qsTr("Delete stack")
        acceptLabel: qsTr("Delete")
        onAccepted: stackState.performDeleteStack(stackState.selectedStackId)
    }

    AppDialog {
        id: previewOffConfirm
        title: qsTr("Turn Preview off")
        acceptLabel: qsTr("Turn Off")
        onAccepted: stackState.performPreviewOff(stackState.selectedStackId)
    }

    AppDialog {
        id: removeLibraryConfirm
        property string asked: ""
        title: qsTr("Remove library")
        blurb: libraryState.deleteFolder ? libraryState.deleteFolderMessage : removeLibraryConfirm.asked
        acceptLabel: libraryState.deleteFolder ? qsTr("Delete Library") : qsTr("Remove")
        acceptEnabled: libraryState.canConfirmRemove
        onAccepted: libraryState.performRemoveLibrary(libraryState.selectedLibraryId)

        FormRow {
            Layout.fillWidth: true
            visible: libraryState.canDeleteFolder
            label: qsTr("Also delete its folder from this computer")
            ToggleSwitch {
                checked: libraryState.deleteFolder
                onToggled: libraryState.deleteFolder = !libraryState.deleteFolder
            }
        }
        Text {
            Layout.fillWidth: true
            visible: !libraryState.canDeleteFolder && !!libraryState.selectedLibrary
                     && !libraryState.selectedLibrary.missing
            wrapMode: Text.WordWrap
            text: qsTr("SeventhSlide did not make this folder, so it will not delete it. To delete "
                       + "it, use %1.").arg(Platform.showFolderText)
            color: Style.textMuted
            font.pixelSize: Style.fontSmall
        }
        Text {
            Layout.fillWidth: true
            visible: libraryState.deleteFolder
            wrapMode: Text.WordWrap
            text: qsTr("Type “%1” to confirm.").arg(libraryState.selectedLibrary
                                                    ? String(libraryState.selectedLibrary.name) : "")
            color: Style.textMuted
            font.pixelSize: Style.fontSmall
        }
        DialogField {
            Layout.fillWidth: true
            visible: libraryState.deleteFolder
            text: libraryState.deleteTyped
            onEdited: function (v) { libraryState.deleteTyped = v }
            onSubmitted: removeLibraryConfirm.submit()
        }
    }

    AppDialog {
        id: cloudFolderConfirm
        title: qsTr("Folder synced to the cloud")
        acceptLabel: qsTr("Add Anyway")
        onAccepted: libraryState.performAttachLibrary(libraryState.pendingFolder)
    }

    AppDialog {
        id: deleteProfileConfirm
        title: qsTr("Delete profile")
        acceptLabel: qsTr("Delete")
        onAccepted: libraryState.performDeleteProfile(libraryState.selectedProfileId)
    }

    AppPromptDialog {
        id: syncPasswordPrompt
        label: qsTr("The library’s admin password")
        acceptLabel: qsTr("OK")
        password: true
        onAccepted: syncState.submitPassword(syncPasswordPrompt.value)
    }

    ComputerEditDialog {
        id: computerEdit
        syncState: syncState
    }

    AppDialog {
        id: syncConfirm
        property string action: ""
        onAccepted: syncState.confirmed(syncConfirm.action)
    }

    AppDialog {
        id: syncNotice
        acknowledge: true
    }

    // A new admin password, typed twice; after the current one or the
    // recovery code when it replaces one.
    AppDialog {
        id: newPasswordDialog
        property string mode: "online"
        readonly property string problem: {
            const fresh = newPasswordField.text
            if (newPasswordDialog.mode !== "online" && firstField.text.trim().length === 0)
                return newPasswordDialog.mode === "recover" ? qsTr("Enter the recovery code.")
                                                           : qsTr("Enter the current password.")
            if (fresh.length === 0)
                return ""
            const rule = syncState.passphraseProblem(fresh)
            if (rule.length)
                return rule
            if (againField.text.length && againField.text !== fresh)
                return qsTr("The two new passwords are not the same.")
            return ""
        }
        function askFor(mode) {
            newPasswordDialog.mode = mode
            firstField.text = ""
            newPasswordField.text = ""
            againField.text = ""
            newPasswordDialog.title = mode === "online" ? qsTr("Put this library online")
                                    : mode === "recover" ? qsTr("Set a new password with the recovery code")
                                    : qsTr("Change the admin password")
            newPasswordDialog.blurb = mode === "online"
                    ? qsTr("Choose the library’s admin password: a few ordinary words is best. It is "
                           + "asked for to invite or remove computers, on any editor computer.")
                    : ""
            newPasswordDialog.open = true
        }
        danger: false
        acceptLabel: newPasswordDialog.mode === "online" ? qsTr("Put Online") : qsTr("Change")
        acceptEnabled: newPasswordDialog.problem.length === 0 && newPasswordField.text.length > 0
                       && againField.text === newPasswordField.text
        onAccepted: syncState.submitNewPassword(newPasswordDialog.mode, firstField.text.trim(),
                                                newPasswordField.text)
        // No password is left in a field once the card closes: after
        // onAccepted has read them, which runs once the card has closed.
        function forget() {
            if (newPasswordDialog.open)
                return
            firstField.text = ""
            newPasswordField.text = ""
            againField.text = ""
        }
        onClosed: Qt.callLater(newPasswordDialog.forget)

        Text {
            visible: newPasswordDialog.mode !== "online"
            text: newPasswordDialog.mode === "recover" ? qsTr("Recovery code") : qsTr("Current password")
            color: Style.textMuted
            font.pixelSize: Style.fontSmall
        }
        DialogField {
            id: firstField
            visible: newPasswordDialog.mode !== "online"
            Layout.fillWidth: true
            password: newPasswordDialog.mode === "change"
            onEdited: function (v) { firstField.text = v }
        }
        Text {
            text: qsTr("New password")
            color: Style.textMuted
            font.pixelSize: Style.fontSmall
        }
        DialogField {
            id: newPasswordField
            Layout.fillWidth: true
            password: true
            onEdited: function (v) { newPasswordField.text = v }
        }
        Text {
            text: qsTr("The new password again")
            color: Style.textMuted
            font.pixelSize: Style.fontSmall
        }
        DialogField {
            id: againField
            Layout.fillWidth: true
            password: true
            onEdited: function (v) { againField.text = v }
        }
        Text {
            Layout.fillWidth: true
            visible: newPasswordDialog.problem.length > 0
            wrapMode: Text.WordWrap
            text: newPasswordDialog.problem
            color: Style.textWarning
            font.pixelSize: Style.fontSmall
        }
    }

    // A code shown once: an invite to pass on, or a recovery code to write
    // down.
    AppDialog {
        id: codeDialog
        property string code: ""
        property bool qr: false
        function show(title, blurb, code, qr) {
            codeDialog.title = title
            codeDialog.blurb = blurb
            codeDialog.code = code
            codeDialog.qr = qr
            codeDialog.open = true
        }
        acknowledge: true
        cardWidth: 520
        onClosed: codeDialog.code = ""

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: codeText.implicitHeight + 16
            radius: Style.radiusSmall
            color: Style.dialogField
            border.color: Style.dialogControlBorder
            TextEdit {
                id: codeText
                anchors.fill: parent
                anchors.margins: 8
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                text: codeDialog.code
                color: Style.text
                font.family: Platform.monoFamily
                font.pixelSize: codeDialog.qr ? Style.fontSmall : Style.fontBody
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Image {
                id: codeQr
                visible: codeDialog.qr && codeDialog.code.length > 0
                readonly property int modules: codeQr.visible ? QrCode.extent(codeDialog.code) : 1
                readonly property int side: codeQr.modules * Math.max(2, Math.floor(200 / Math.max(1, codeQr.modules)))
                Layout.preferredWidth: codeQr.visible ? codeQr.side : 0
                Layout.preferredHeight: codeQr.visible ? codeQr.side : 0
                source: codeQr.visible ? QrCode.url(codeDialog.code) : ""
                sourceSize: Qt.size(codeQr.side, codeQr.side)
                smooth: false
            }
            Item { Layout.fillWidth: true }
            AppButton {
                Layout.alignment: Qt.AlignTop
                size: "control"
                label: qsTr("Copy")
                iconSource: Icons.url("copy", Style.textSoft, 2)
                onClicked: Clipboard.setText(codeDialog.code)
            }
        }
    }

    AppPromptDialog {
        id: libraryNamePrompt
        label: qsTr("Name")
        onAccepted: libraryState.submitName(libraryNamePrompt.value)
    }

    // An invite pasted to join a synced library, and what it is for.
    AppDialog {
        id: joinDialog
        readonly property var info: libraryState.inviteInfo
        title: qsTr("Join a synced library")
        blurb: qsTr("Paste the invite another computer of the library made.")
        danger: false
        acceptLabel: qsTr("Join")
        acceptEnabled: !!joinDialog.info.text && !(root.sync && root.sync.working)
        onAccepted: libraryState.joinLibrary()

        DialogField {
            Layout.fillWidth: true
            text: libraryState.inviteCode
            placeholder: qsTr("Paste the code")
            onEdited: function (v) { libraryState.inviteCode = v }
        }
        Text {
            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.WordWrap
            text: joinDialog.info.error || joinDialog.info.text || ""
            color: joinDialog.info.error ? Style.textWarning : Style.textCaption
            font.pixelSize: Style.fontSmall
        }
    }

    FolderDialog {
        id: libraryFolderDialog
        title: qsTr("Choose a library folder")
        onAccepted: libraryState.attachLibrary(selectedFolder.toString())
    }

    FileDialog {
        id: fontFilesDialog
        title: qsTr("Add fonts")
        fileMode: FileDialog.OpenFiles
        nameFilters: [qsTr("Fonts (*.ttf *.otf *.ttc *.otc)"), qsTr("All files (*)")]
        onAccepted: fontState.addFiles(fontFilesDialog.selectedFiles)
    }

    // This computer's picture is read as the library's pictures are (Qt's
    // image readers), so it offers the files the library takes.
    FileDialog {
        id: pictureFileDialog
        objectName: "syncPictureDialog"
        title: qsTr("Choose a picture for this computer")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Images (%1)").arg(root.session.media.mediaFilePatterns(MediaKind.Image)),
                      qsTr("All files (*)")]
        onAccepted: syncState.setPictureFile(selectedFile.toString())
    }

    FileDialog {
        id: bibleDialog
        title: qsTr("Import bible")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Bible XML (*.xml)"), qsTr("All files (*)")]
        onAccepted: bibleState.importBible(selectedFile.toString())
    }


    /// One page of the window: made the first time it shows, and kept.
    component SettingsPage: LazyLoader {
        id: settingsPage
        /// The page is the one showing.
        property bool shown: false

        anchors.fill: parent
        visible: settingsPage.shown
        onShownChanged: {
            if (settingsPage.shown)
                settingsPage.load()
        }
        Component.onCompleted: {
            if (settingsPage.shown)
                settingsPage.load()
        }
    }

    /// A dragged row: its tile and its name, like the rows it lifts from.
    component DragGhost: Rectangle {
        id: ghost
        property string iconSource: ""
        property string label: ""

        z: 100
        height: Style.groupRowH
        radius: Style.radiusSmall
        color: Style.groupSel
        border.color: Style.groupEdge
        opacity: 0.92

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            spacing: 10
            HandleTile {
                iconSource: ghost.iconSource
            }
            Text {
                Layout.fillWidth: true
                text: ghost.label
                color: Style.textStrong
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }
    }
}
