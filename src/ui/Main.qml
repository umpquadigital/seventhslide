pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

Window {
    id: root
    width: 1280
    height: 720
    visible: true
    title: qsTr("SeventhSlide")
    color: Style.pageBg

    // Handed over by main() (and by the UI tests) before the window loads.
    required property SessionFacade session
    required property RemoteController remote
    /// This computer's sync; null when it is off (and in tests).
    property SyncController sync: null

    /// Something else was worked: the library's search and the controller's
    /// address bar give up the keys, so the operator's shortcuts work again.
    function blurFields() {
        libraryPanel.clearFieldFocus()
        controller.clearFieldFocus()
    }

    /// Where the panels sit, kept on this computer.
    DockLayout {
        id: dockLayout
    }

    // Operator shortcuts. Window shortcuts win over the focused item's keys, so
    // they stand down while a modal is open: there the keys belong to the modal
    // (arrows nudge the selected object) and must not move the live output.
    // Left/Right (and aliases) go through the controller so "Follow live line"
    // only runs for intentional next/prev - not for clicking a line.
    readonly property bool operatorKeysEnabled: !!root.session && !Modals.open

    Shortcut {
        sequences: ["Right", "Down", "PgDown"]
        enabled: root.operatorKeysEnabled
        onActivated: controller.navigateNext()
    }
    Shortcut {
        sequences: ["Left", "Up", "PgUp"]
        enabled: root.operatorKeysEnabled
        onActivated: controller.navigatePrev()
    }
    // Transition: what is cued on the focused stack goes live, by its theme's
    // transition.
    Shortcut {
        sequence: "T"
        enabled: root.operatorKeysEnabled
        onActivated: controller.take()
    }

    // Blank and Freeze. B / F: every output not exempt from global blank and
    // freeze. Shift+B / Shift+F: the stack the controller shows, on every
    // output. An output's number (its card's place, 1 to 9), then B or F: that
    // output alone. Shift+Esc lifts every blank and freeze.
    /// The card whose number was pressed, waiting for B or F; -1 for none.
    /// It waits only a moment, so a stray number cannot redirect a later B.
    property int armedOutput: -1

    function armOutput(index) {
        const outputs = root.session.outputsOn
        if (index >= outputs.rowCount()) {
            root.disarmOutput()
            return
        }
        root.armedOutput = index
        armedOutputTimer.restart()
    }

    function disarmOutput() {
        root.armedOutput = -1
        armedOutputTimer.stop()
    }

    /// B or F: the armed output's, else global.
    function blankOrFreeze(freeze) {
        const index = root.armedOutput
        root.disarmOutput()
        const live = root.session.live
        if (index < 0) {
            if (freeze)
                live.toggleGlobalFreeze()
            else
                live.toggleGlobalBlank()
            return
        }
        const outputs = root.session.outputsOn
        const id = outputs.data(outputs.index(index, 0), OutputListModel.IdRole) || ""
        if (freeze)
            live.toggleOutputFreeze(id)
        else
            live.toggleOutputBlank(id)
    }

    Timer {
        id: armedOutputTimer
        interval: 1500
        onTriggered: root.armedOutput = -1
    }
    Instantiator {
        model: 9
        delegate: Shortcut {
            id: digitKey
            required property int index
            sequence: String(digitKey.index + 1)
            enabled: root.operatorKeysEnabled
            onActivated: root.armOutput(digitKey.index)
        }
    }
    Shortcut {
        sequence: "B"
        enabled: root.operatorKeysEnabled
        onActivated: root.blankOrFreeze(false)
    }
    Shortcut {
        sequence: "F"
        enabled: root.operatorKeysEnabled
        onActivated: root.blankOrFreeze(true)
    }
    Shortcut {
        sequence: "Shift+B"
        enabled: root.operatorKeysEnabled
        onActivated: {
            root.disarmOutput()
            controller.toggleStackBlank()
        }
    }
    Shortcut {
        sequence: "Shift+F"
        enabled: root.operatorKeysEnabled
        onActivated: {
            root.disarmOutput()
            controller.toggleStackFreeze()
        }
    }
    // The system's own key for an app's settings: Command+, on macOS, where
    // people expect it; none on Windows, where there is no such key.
    Shortcut {
        sequences: [StandardKey.Preferences]
        enabled: root.operatorKeysEnabled && root.session.ready
        onActivated: root.settings().openDialog()
    }
    Shortcut {
        sequence: "Shift+Esc"
        enabled: root.operatorKeysEnabled
        onActivated: {
            root.disarmOutput()
            root.session.live.clearBlankFreeze()
        }
    }

    // Click-away blur as a *parent* of the chrome (not a top overlay). Children
    // receive hover/press first, so cursorShape on buttons and fields still works.
    // Blank areas with no child MouseArea bubble here and blur the fields.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function (mouse) {
            const focused = root.activeFocusItem
            if (focused) {
                const local = mapToItem(focused, mouse.x, mouse.y)
                if (local.x >= 0 && local.y >= 0
                        && local.x < focused.width && local.y < focused.height) {
                    return
                }
            }
            root.blurFields()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // The Controller, with the Service, Library, Outputs and Audio
            // Mixer panels docked round it.
            DockArea {
                id: dock
                Layout.fillWidth: true
                Layout.fillHeight: true
                layout: dockLayout
                controllerMinWidth: controller.minimumWidth

                ControllerColumn {
                    id: controller
                    readonly property var place: dock.geometry.places.controller ?? null
                    x: controller.place ? controller.place.x : 0
                    y: controller.place ? controller.place.y : 0
                    width: controller.place ? controller.place.width : 0
                    height: controller.place ? controller.place.height : 0
                    session: root.session
                    onInteracted: root.blurFields()
                }

                DockPanel {
                    area: dock
                    dockId: "service"
                    title: qsTr("Service")

                    ServicePanel {
                        id: servicePanel
                        anchors.fill: parent
                        session: root.session
                        onInteracted: root.blurFields()
                        onFailed: message => operationErrorDialog.ask(message)
                        onDropIncoming: dockLayout.setCollapsed("service", false)
                    }
                }

                DockPanel {
                    area: dock
                    dockId: "library"
                    title: qsTr("Library")

                    LibraryPanel {
                        id: libraryPanel
                        anchors.fill: parent
                        session: root.session
                        serviceHost: servicePanel
                        onRequestSaveAsFreeform: function (songId, name) {
                            root.saveAsFreeform().prepareSong(songId)
                            root.saveAsFreeform().ask(name)
                        }
                        onRequestSaveBibleAsFreeform: function (verseStart, verseEnd, name) {
                            root.saveAsFreeform().prepareBible(verseStart, verseEnd)
                            root.saveAsFreeform().ask(name)
                        }
                    }
                }

                DockPanel {
                    area: dock
                    dockId: "outputs"
                    title: qsTr("Outputs")

                    OutputsPanel {
                        anchors.fill: parent
                        session: root.session
                        armedIndex: root.armedOutput
                        onInteracted: root.blurFields()
                        onOutputSettingsRequested: name => root.outputEditor().openFor(name)
                    }
                }

                DockPanel {
                    area: dock
                    dockId: "mixer"
                    title: qsTr("Audio Mixer")

                    AudioMixerPanel {
                        anchors.fill: parent
                        session: root.session
                        onFailed: message => operationErrorDialog.ask(message)
                    }
                }
            }

            AppBar {
                Layout.fillWidth: true
                session: root.session
                sync: root.sync
                layout: dockLayout
                onInteracted: root.blurFields()
                onLibrariesRequested: root.settings().openPage("library")
                onCollaborationRequested: root.collaboration().show()
                onSettingsRequested: root.settings().openDialog()
                onSyncRequested: root.settings().openPage("sync")
                onFontsRequested: root.settings().openPage("fonts")
            }
        }
    }

    // The windows the bar and the panels open are made the first time they
    // are asked for (LazyLoader). An output opened from Settings > Outputs, or
    // a song from Settings > Fonts, draws over Settings, and closing it goes
    // back there.
    function settings(): AppSettingsDialog {
        return appSettingsLoader.load() as AppSettingsDialog
    }
    function outputEditor(): OutputEditDialog {
        return outputEditorLoader.load() as OutputEditDialog
    }
    function settingsSongEditor(): SongEditDialog {
        return songEditorLoader.load() as SongEditDialog
    }
    function collaboration(): CollaborationWindow {
        return collaborationLoader.load() as CollaborationWindow
    }
    function saveAsFreeform(): SaveAsFreeformDialog {
        return saveAsFreeformLoader.load() as SaveAsFreeformDialog
    }

    /// A window opened over Settings has closed: Settings takes the keys
    /// again, and shows what that window changed.
    function backToSettings() {
        const shown = appSettingsLoader.item as AppSettingsDialog
        if (shown && shown.open) {
            shown.editorClosed()
            shown.forceActiveFocus()
        }
    }

    LazyLoader {
        id: appSettingsLoader
        objectName: "AppSettingsDialog"
        sourceComponent: AppSettingsDialog {
            session: root.session
            remote: root.remote
            sync: root.sync
            onOutputSettingsRequested: name => root.outputEditor().openFor(name)
            onSongEditRequested: function (id, item) {
                if (item)
                    root.settingsSongEditor().openItem(id)
                else
                    root.settingsSongEditor().openLibrary(id)
            }
        }
    }

    LazyLoader {
        id: collaborationLoader
        objectName: "CollaborationWindow"
        sourceComponent: CollaborationWindow {
            sync: root.sync
            onFailed: message => operationErrorDialog.ask(message)
        }
    }

    LazyLoader {
        id: outputEditorLoader
        objectName: "OutputEditDialog"
        sourceComponent: OutputEditDialog {
            id: outputEditor
            session: root.session
            z: Style.sheetZ + 1
            onOpenChanged: {
                if (!outputEditor.open)
                    root.backToSettings()
            }
        }
    }

    LazyLoader {
        id: songEditorLoader
        objectName: "SongEditDialog"
        sourceComponent: SongEditDialog {
            id: songEditor
            session: root.session
            z: Style.sheetZ + 1
            onOpenChanged: {
                if (!songEditor.open)
                    root.backToSettings()
            }
        }
    }

    // The slide editor opens itself when the session opens a document, so it
    // is made then.
    LazyLoader {
        id: slideEditorLoader
        objectName: "SlideEditorWindow"
        sourceComponent: SlideEditorWindow {
            session: root.session
        }
    }

    Connections {
        target: root.session ? root.session.slideEdit : null
        function onDocumentChanged() {
            if (root.session.slideEdit.active)
                slideEditorLoader.load()
        }
    }

    LazyLoader {
        id: saveAsFreeformLoader
        objectName: "SaveAsFreeformDialog"
        sourceComponent: SaveAsFreeformDialog {
            session: root.session
            onFailed: message => operationErrorDialog.ask(message)
        }
    }

    // A library that will not open leaves every action disabled. Say why, so it
    // does not read as "my songs are gone".
    AppDialog {
        id: databaseErrorDialog
        // With the library list refused there is no library to name.
        title: root.session && !root.session.libraries.activeId
               ? qsTr("Could not read the library list") : qsTr("Could not open the library")
        acknowledge: true
        blurb: root.session ? root.session.databaseError : ""
        open: !!root.session && !root.session.ready && !!root.session.databaseError
    }

    // A command that failed with no way to say so in its own dialog (a save,
    // an import, a move). Shown here rather than only logged, so a failure
    // never reads as "nothing happened".
    AppDialog {
        id: operationErrorDialog
        title: qsTr("That did not work")
        acknowledge: true
    }

    Connections {
        target: root.session
        function onErrorOccurred(message) {
            operationErrorDialog.ask(message)
        }
    }

    // An output changed shape: what was laid out on it was carried over once,
    // and the operator adjusts what looks wrong.
    AppDialog {
        id: canvasFittedDialog
        title: qsTr("Fitted to the new canvas")
        acknowledge: true
    }

    Connections {
        target: root.session ? root.session.outputSettings : null
        function onCanvasFitted(message) {
            canvasFittedDialog.ask(message)
        }
    }
}
