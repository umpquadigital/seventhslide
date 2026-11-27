pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Dialogs
import SeventhSlide

// One tab of the library panel: Songs, Freeform, Bibles, Videos, Images, Audio,
// and behind the strip's arrow Browser and Live.
// LibraryPanel runs its toolbar, search field and Insert key through this
// interface and never asks which tab is showing; each tab sets what it offers
// and overrides the actions it has.
Item {
    id: tab

    required property SessionFacade session
    /// The Service panel, for library -> service drag-to-add.
    property ServicePanel serviceHost: null
    /// The tab showing. Set by the panel.
    property bool current: false
    /// The panel's padding round the tab: a marquee may start in it.
    property real marqueeBleed: 0
    // A press on a row may become a drag onto the service (LibraryRowPress).
    property bool libDragging: false
    property real pressX: 0
    property real pressY: 0
    property real pressMs: 0
    readonly property real dragThreshold: 12

    /// Its name where it is remembered (the tab showing, per computer).
    property string tabId
    /// The tab strip's label: the name of the kind it holds.
    readonly property string title: ContentTypes.ofTab(tab.tabId)?.label ?? ""
    property string searchPlaceholder
    /// What the search field holds on this tab.
    property string search: ""

    // The toolbar buttons this tab shows.
    property bool offersCreate: false
    property string createIcon: "plus"
    /// Ways to create, as ``[{id, label, icon}]``; ``createChosen(id)`` runs
    /// the one picked. The create button runs the only one at once, and
    /// opens a menu of them otherwise.
    property var createChoices: []
    /// The create menu ends with New folder, which runs ``newFolder``.
    property bool offersNewFolder: false
    /// Ways to export what is selected, as ``[{id, label, icon}]``;
    /// ``exportChosen(id)`` runs the one picked. With any, the toolbar shows
    /// an export button, which runs the only one at once and opens a menu of
    /// them otherwise.
    property var exportChoices: []
    readonly property bool offersExport: tab.exportChoices.length > 0
    property bool offersEdit: false
    property bool offersConvert: false
    property bool offersDuplicate: false
    property bool offersDelete: false
    property bool offersViewToggle: false
    /// Controls of the tab's own at the start of the toolbar, in its free
    /// space, with the buttons after them (the Bibles tab's translation
    /// picker), or null.
    property Component toolbarExtra: null

    /// How many selected things Add would put in the service.
    property int addableSelectionCount: 0
    property bool canEdit: false
    property bool canConvert: false
    property bool canDuplicate: false
    property bool canDelete: false
    /// Something is selected that can be exported.
    property bool canExport: false
    readonly property bool serviceOpen: !!tab.session && tab.session.openServiceId >= 0
    readonly property bool canAdd: tab.serviceOpen && tab.addableSelectionCount > 0

    /// A press in the tab: the panel ends a search-field edit.
    signal interacted()

    visible: tab.current

    /// The tab was switched to, or away from.
    function enter() {}
    function leave() {}
    function clearSelection() {}
    /// Close the tab's own menus.
    function closeMenus() {}

    // Toolbar actions; each runs only where its button shows.
    function createChosen(id) {}
    function newFolder() {}
    function exportChosen(id) {}
    function editSelected() {}
    function convertSelected() {}
    function duplicateSelected() {}
    function toggleView() {}
    /// What the delete confirm asks.
    function deleteText() {
        return qsTr("Delete the selected item?")
    }
    /// Delete what is selected. Stops at the first refusal and returns it;
    /// "" when everything went.
    function deleteSelected() {
        return ""
    }
    /// Add what is selected to the open service.
    function addToService() {}

    // The search field's keys. Each returns whether the tab used the key.
    function searchKeyPressed(key) {
        return false
    }
    function searchSubmitted() {
        return false
    }
    function searchEscaped() {
        return false
    }

    // Exports, which the tabs start through these and session.exporter.

    /// Ask which output's slides and where the ZIP file goes, saying
    /// ``what`` is exported and naming the file for ``fileTitle``; then
    /// ``start(outputId, file)`` starts the export and returns its request id.
    function exportImages(what, fileTitle, start) {
        tab._exportStart = start
        const ask = imageExportLoader.load() as ExportImagesDialog
        ask.openFor(what, fileTitle)
    }

    /// Ask for a folder (titling the chooser ``title``); then
    /// ``start(folder)`` starts the export into it and returns its request id.
    function exportToFolder(title, start) {
        tab._exportStart = start
        const chooser = folderExportLoader.load() as FolderDialog
        chooser.title = title
        chooser.currentFolder = tab.session.exporter.lastFolder
        chooser.open()
    }

    /// The exports this tab started, by request id, whose outcome it reports.
    property var _exportRequests: []
    /// What starts the export being asked about (see exportImages).
    property var _exportStart: null

    /// Run ``_exportStart`` with ``args`` and follow the export it starts.
    function _start(...args) {
        const start = tab._exportStart
        tab._exportStart = null
        const requestId = start ? start(...args) : -1
        if (requestId !== undefined && requestId >= 0)
            tab._exportRequests = tab._exportRequests.concat([requestId])
    }

    Connections {
        target: tab.session ? tab.session.exporter : null
        function onExportFinished(requestId, result) {
            if (tab._exportRequests.indexOf(requestId) < 0)
                return
            tab._exportRequests = tab._exportRequests.filter(id => id !== requestId)
            if (!result.ok)
                exportError.ask(result.errors.join("\n"))
            else if (result.notes && result.notes.length > 0)
                tab.tellDone(qsTr("Exported"), result.notes.join("\n"))
        }
    }

    // What an export asks, made the first time a tab exports.
    LazyLoader {
        id: imageExportLoader
        objectName: "ExportImagesDialog"
        sourceComponent: ExportImagesDialog {
            session: tab.session
            onChosen: function (outputId, file) {
                tab._start(outputId, file)
            }
        }
    }

    LazyLoader {
        id: folderExportLoader
        objectName: "FolderDialog"
        sourceComponent: FolderDialog {
            id: folderExport
            onAccepted: tab._start(folderExport.selectedFolder.toString())
        }
    }

    AppDialog {
        id: exportError
        title: qsTr("Could not export")
        acknowledge: true
    }

    AppDialog {
        id: exportNotes
        objectName: "libraryNotes"
        title: qsTr("Exported")
        acknowledge: true
    }

    /// Say how something finished well, under ``title`` (an import's count
    /// and what it left out).
    function tellDone(title, text) {
        exportNotes.title = title
        exportNotes.ask(text)
    }

    /// Ask where a file of ``filter`` (a name filter) ending in ``suffix``
    /// goes, starting at ``suggested``; then ``start(file)`` starts the
    /// export and returns its request id.
    function exportToFile(title, filter, suffix, suggested, start) {
        tab._exportStart = start
        const chooser = fileExportLoader.load() as FileDialog
        chooser.title = title
        chooser.nameFilters = [filter]
        chooser.defaultSuffix = suffix
        chooser.selectedFile = suggested
        chooser.open()
    }

    LazyLoader {
        id: fileExportLoader
        objectName: "FileExportDialog"
        sourceComponent: FileDialog {
            id: fileExport
            fileMode: FileDialog.SaveFile
            onAccepted: tab._start(fileExport.selectedFile.toString())
        }
    }

    // Library -> service drags, through the service column.

    /// Add ``payload`` at the end of the open service. False without a host.
    function applyLibraryPayload(payload) {
        if (!payload || !tab.serviceHost)
            return false
        return tab.serviceHost.applyLibAddPayload(payload, -1)
    }

    function beginLibAdd(payload, label, mapItem, mx, my) {
        if (!tab.serviceHost || !tab.serviceOpen || !payload)
            return false
        const gx = mapItem ? mx : 16
        const gy = mapItem ? my : 16
        return tab.serviceHost.beginLibAddDrag(payload, label, gx, gy)
    }

    function updateLibAdd(mapItem, mx, my) {
        if (!tab.serviceHost || !tab.serviceHost.libAddDragActive)
            return
        tab.serviceHost.updateLibAddDragFrom(mapItem, mx, my)
    }

    function finishLibAdd(mapItem, mx, my) {
        if (!tab.serviceHost || !tab.serviceHost.libAddDragActive)
            return false
        return tab.serviceHost.finishLibAddDragFrom(mapItem, mx, my)
    }

    function cancelLibAdd() {
        if (tab.serviceHost)
            tab.serviceHost.cancelLibAddDrag()
    }
}
