pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect

// The library's Freeform, Videos, Images, Audio and Browser tabs: a folder
// browser over one kind of entry, shown as a list or as icons. Every tab's
// ``library`` has the same folder calls (FolderLibrary). Media rows are files,
// keyed by filename; Freeform documents and web pages are keyed by id.
LibraryTab {
    id: root
    /// The media kind listed (MediaKind.Image, .Video or .Audio); unused when
    /// ``freeform`` or ``web`` is set.
    property int kind: MediaKind.Image
    /// List the Freeform library instead of a media kind.
    property bool freeform: false
    /// List the Browser tab's web pages instead of a media kind.
    property bool web: false

    searchPlaceholder: root.kindText.searchPlaceholder
    onSearchChanged: {
        if (root.library)
            root.library.setSearch(root.search)
    }
    offersCreate: true
    createIcon: "plus"
    // A Freeform item or a web page is made here; video and sound can come
    // from the internet too, images only from this computer.
    createChoices: root.isFreeform ? [{ id: "new", label: qsTr("New Freeform item"), icon: "freeform" },
                                      { id: "importFile", label: qsTr("Import Freeform file…"), icon: "import" }]
                 : root.isWeb ? [{ id: "new", label: qsTr("New web page…"), icon: "web" }]
                 : root.isVideo || root.isAudio
                   ? [{ id: "computer", label: qsTr("From this computer…"), icon: "import" },
                      { id: "internet", label: qsTr("From the internet…"), icon: "globe" }]
                 : [{ id: "computer", label: qsTr("From this computer…"), icon: "import" }]
    offersNewFolder: true
    // Freeform items leave as their slides; media files leave as the files
    // they are. Web pages are addresses, kept only here.
    exportChoices: root.isFreeform ? [{ id: "images", label: qsTr("As images…"), icon: "image" },
                                      { id: "file", label: qsTr("As a Freeform file…"), icon: "freeform" }]
                 : root.isWeb ? []
                 : [{ id: "files", label: qsTr("To a folder…"), icon: "folder" }]
    canExport: root.selectedEntryCount > 0
    offersEdit: true
    offersDuplicate: root.isItemLibrary
    offersDelete: true
    offersConvert: !root.isItemLibrary
    offersViewToggle: true
    /// Files, folders and items selected (not the ".." row).
    readonly property int selectedEntryCount: root.selectedFilenames.length
                                              + root.selectedFolderIds.length
                                              + root.selectedItemIds.length
    addableSelectionCount: root.selectedEntryCount
    canEdit: root.selectedEntryCount === 1
    canDuplicate: root.isItemLibrary && root.selectedItemCount > 0
    canDelete: root.selectedEntryCount > 0
    // Files only: a folder is not one thing to convert.
    canConvert: !root.isItemLibrary && root.selectedFilenames.length > 0
                && root.selectedFolderIds.length === 0

    // What the list or the icon view says with nothing to show.
    component EmptyNote: Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: 24
        z: 1
        color: Style.textMuted
        font.pixelSize: Style.fontUi
    }

    component EntrySelection: ListSelection {
        keyRole: -1  // keyed by keyOf() below, not by one role
        /// An entry's key names its kind: see entryKey().
        function keyOf(row) {
            const e = root.browserEntry(row)
            return e ? root.entryKey(e.kind, e.entryId, e.filename) : null
        }
    }

    /// The selected entries, keyed by kind: ``folder:<id>``, ``file:<filename>``,
    /// and ``item:<id>`` for Freeform, whose file rows are keyed by ``entryId``
    /// (``freeform_id``), not filename.
    readonly property EntrySelection entrySelection: EntrySelection {
        model: root.browserModel
        view: root.iconsMode ? iconFlick : listView
        tiles: root.iconsMode ? iconFlow : null
        onChanged: {
            if (keys.length)
                root.selectedParent = false
            root.selectionChanged()
        }
    }
    readonly property var selectedFilenames: root.keysOfKind("file")
    readonly property var selectedFolderIds: root.keysOfKind("folder").map(Number)
    readonly property var selectedItemIds: root.keysOfKind("item").map(Number)
    // The last picked of each kind.
    readonly property string selectedFilename:
        root.selectedFilenames.length ? root.selectedFilenames[root.selectedFilenames.length - 1] : ""
    readonly property var selectedFolderId:
        root.selectedFolderIds.length ? root.selectedFolderIds[root.selectedFolderIds.length - 1] : -1
    readonly property var selectedItemId:
        root.selectedItemIds.length ? root.selectedItemIds[root.selectedItemIds.length - 1] : -1
    /// Visual-only selection for the ".." parent row (double-click still goes up).
    /// Exclusive with entries.
    property bool selectedParent: false
    readonly property int selectedItemCount: root.selectedItemIds.length

    readonly property int selectionCount: root.selectedEntryCount
                                          + (root.selectedParent ? 1 : 0)

    readonly property bool marqueeActive: root.entrySelection.marqueeActive

    signal selectionChanged()

    readonly property bool isImage: !root.isItemLibrary && root.kind === MediaKind.Image
    readonly property bool isVideo: !root.isItemLibrary && root.kind === MediaKind.Video
    readonly property bool isAudio: !root.isItemLibrary && root.kind === MediaKind.Audio
    readonly property bool isFreeform: root.freeform
    readonly property bool isWeb: root.web
    /// Rows are items of a library keyed by id (Freeform, web pages), not files.
    readonly property bool isItemLibrary: root.freeform || root.web
    /// The library listed: its browser, folders and items.
    readonly property FolderLibrary library: !root.session ? null
                                 : root.isWeb ? root.session.webLibrary
                                 : root.isFreeform ? root.session.freeformLibrary
                                 : root.session.media.library(root.kind)

    /// The words and glyph each kind shows. ``serviceItem`` / ``serviceFolder``
    /// are the payload kinds a selected file / folder adds to a service.
    readonly property var kindText: ({
        freeform: {
            searchPlaceholder: qsTr("Search Freeform…"),
            glyph: "freeform",
            emptyRoot: qsTr("No Freeform items"),
            emptyFolder: qsTr("No Freeform items in this folder"),
            deleteOne: qsTr("Permanently delete this Freeform item from the library?"),
            deleteFolder: qsTr("Delete this folder and all Freeform items inside it?"),
            serviceFolder: "freeform_folder"
        },
        web: {
            searchPlaceholder: qsTr("Search Browser…"),
            glyph: "web",
            emptyRoot: qsTr("No web pages"),
            emptyFolder: qsTr("No web pages in this folder"),
            deleteOne: qsTr("Delete this web page from the library?"),
            deleteFolder: qsTr("Delete this folder and all web pages inside it?"),
            serviceItem: "web",
            serviceFolder: "web_folder"
        },
        image: {
            searchPlaceholder: qsTr("Search Images…"),
            glyph: "image",
            emptyRoot: qsTr("No images"),
            emptyFolder: qsTr("No media in this folder"),
            deleteOne: qsTr("Permanently delete this image from the library?"),
            deleteFolder: qsTr("Delete this folder and all images inside it from disk?"),
            renameTitle: qsTr("Rename image"),
            importTitle: qsTr("Import images"),
            filterLabel: qsTr("Images"),
            serviceItem: "image",
            serviceFolder: "image_folder"
        },
        video: {
            searchPlaceholder: qsTr("Search Videos…"),
            glyph: "video",
            emptyRoot: qsTr("No videos"),
            emptyFolder: qsTr("No media in this folder"),
            deleteOne: qsTr("Delete this video? The file will be removed from disk."),
            deleteFolder: qsTr("Delete this folder and all videos inside it from disk?"),
            renameTitle: qsTr("Rename video"),
            importTitle: qsTr("Import videos"),
            filterLabel: qsTr("Videos"),
            serviceItem: "video",
            serviceFolder: "video_folder"
        },
        audio: {
            searchPlaceholder: qsTr("Search Audio…"),
            glyph: "audio",
            emptyRoot: qsTr("No audio"),
            emptyFolder: qsTr("No audio in this folder"),
            deleteOne: qsTr("Delete this audio file? The file will be removed from disk."),
            deleteFolder: qsTr("Delete this folder and all audio inside it from disk?"),
            renameTitle: qsTr("Rename audio file"),
            importTitle: qsTr("Import audio"),
            filterLabel: qsTr("Audio"),
            serviceItem: "audio",
            serviceFolder: "audio_folder"
        }
    })[root.isFreeform ? "freeform" : root.isWeb ? "web" : root.isImage ? "image"
       : root.isVideo ? "video" : "audio"]

    readonly property var browserModel: root.library ? root.library.browser : null
    readonly property var currentFolderId: root.library ? root.library.folderId : -1
    readonly property bool atRoot: root.currentFolderId < 0
    readonly property bool iconsMode: root.session && root.session.media.mediaViewMode === "icons"

    readonly property int listGap: 3
    readonly property int rowHeight: Style.listRowH
    readonly property int listThumbW: 40
    readonly property int listThumbH: 22
    /// Minimum icon tile width. Column count is taken from the full
    /// Flickable width so a scrollbar gutter cannot drop a column (same
    /// approach as the controller image gallery). Tiles then stretch to
    /// fill the inner row.
    readonly property int iconMinTileW: 80
    readonly property int iconGap: 6
    /// One line of a tile's caption. Every tile lays its lines out at this
    /// height, so all are the same height and a row's names start level.
    readonly property int iconCaptionLineH: Math.ceil(captionMetrics.lineSpacing)
    readonly property real iconTileW: iconFlow && iconFlow.cellW > 0
                                      ? iconFlow.cellW : iconMinTileW

    property string folderNameMode: "create"  // create | rename | renameFile
    property string renameLockedExt: ""
    property var dropHighlightId: null
    property string dropInsertKey: ""
    property var pendingMove: null
    property bool dragging: false
    property string dragGhostLabel: ""
    property bool dropApplied: false
    property real ghostX: 0
    property real ghostY: 0
    property real dragPressX: 0
    property real dragPressY: 0
    property real dragPressMs: 0
    /// Edge auto-scroll while dragging (list + icon grid).
    readonly property real dragScrollEdge: 36
    readonly property real dragScrollStep: 14
    property int dragScrollDir: 0  // -1 up, 0 none, +1 down
    /// Cursor position in root coords (stable while content scrolls under a held drag).
    property real dragPointerRootX: 0
    property real dragPointerRootY: 0
    /// Preserve viewport after model reset on drop/reorder.
    property real restoreContentY: -1

    FontMetrics {
        id: captionMetrics
        font.pixelSize: Style.fontCaption
    }

    /// Browser tiles have their own solid glyphs (tile/folder, tile/video, ...).
    function iconSvg(kind, ink) {
        return Icons.url("tile/" + kind, ink)
    }

    function fileGlyphKind() {
        return root.kindText.glyph
    }

    /// Rows the view showing has. A view's ``count`` follows the model as it
    /// changes, which a ``rowCount()`` call in a binding does not.
    readonly property int shownCount: root.iconsMode ? iconRepeater.count : listView.count
    /// Nothing to show: no rows, or in a folder only the ".." row (which is
    /// left out while searching).
    readonly property bool showsNothing: root.shownCount === 0
                                         || (root.shownCount === 1 && !root.atRoot
                                             && !root.search.trim().length)
    readonly property string emptyText: root.search.trim().length > 0
                                        ? qsTr("Nothing matches your search.")
                                        : root.atRoot ? root.kindText.emptyRoot
                                                      : root.kindText.emptyFolder

    function clearSelection() {
        root.selectedParent = false
        root.entrySelection.clear()
    }

    function browserEntry(row) {
        const model = root.browserModel
        if (!model || row < 0 || row >= model.rowCount())
            return null
        const ix = model.index(row, 0)
        return {
            kind: String(model.data(ix, MediaBrowserModel.KindRole) || ""),
            entryId: Number(model.data(ix, MediaBrowserModel.IdRole)),
            filename: String(model.data(ix, MediaBrowserModel.FilenameRole) || "")
        }
    }

    function isFileSelected(fn) {
        return root.entrySelection.contains(root.entryKey("file", -1, fn))
    }

    function isFolderSelected(id) {
        return root.entrySelection.contains(root.entryKey("folder", id, ""))
    }

    function selectParent() {
        root.entrySelection.clear()
        root.selectedParent = true
        root.selectionChanged()
    }

    function selectFile(fn, multi) {
        root.pick(root.entryKey("file", -1, fn), multi)
    }

    function isItemSelected(id) {
        return root.entrySelection.contains(root.entryKey("file", id, ""))
    }

    function selectItem(id, multi) {
        root.pick(root.entryKey("file", id, ""), multi)
    }

    function isEntryFileSelected(filename, entryId) {
        return root.entrySelection.contains(root.entryKey("file", entryId, filename))
    }

    function selectFolder(id, multi) {
        root.pick(root.entryKey("folder", id, ""), multi)
    }

    /// The selection key of an entry, or null for one that cannot be selected
    /// (the parent row, a missing id or filename).
    function entryKey(kind, entryId, filename) {
        if (kind === "folder")
            return (entryId !== undefined && entryId >= 0) ? "folder:" + Number(entryId) : null
        if (kind !== "file")
            return null
        if (root.isItemLibrary)
            return (entryId !== undefined && entryId >= 0) ? "item:" + Number(entryId) : null
        return (filename && filename.length) ? "file:" + filename : null
    }

    /// The selected keys of ``kind`` ("folder", "file", "item"), without the prefix.
    function keysOfKind(kind) {
        const prefix = kind + ":"
        const out = []
        const keys = root.entrySelection.keys
        for (let i = 0; i < keys.length; ++i) {
            if (keys[i].startsWith(prefix))
                out.push(keys[i].slice(prefix.length))
        }
        return out
    }

    function pick(key, multi) {
        if (key === null)
            return
        if (multi)
            root.entrySelection.toggle(key)
        else
            root.entrySelection.selectOnly(key)
    }

    function applyEntryPress(kind, entryId, filename, rowIndex, modifiers) {
        if (kind === "parent") {
            root.selectParent()
            return "select"
        }
        return root.entrySelection.press(root.entryKey(kind, entryId, filename), modifiers)
    }

    function collapsePressSelection() {
        root.entrySelection.release()
    }

    function beginMarqueeFrom(item, mx, my, additive) {
        root.selectedParent = false
        root.dragging = false
        root.pendingMove = null
        if (root.serviceHost && root.serviceHost.libAddDragActive)
            root.serviceHost.cancelLibAddDrag()
        root.entrySelection.beginMarquee(item, mx, my, additive)
    }

    function updateMarqueeFrom(item, mx, my) {
        root.entrySelection.updateMarquee(item, mx, my)
    }

    function endMarquee() {
        root.entrySelection.endMarquee()
    }

    // ---- Library actions: the library toolbar, on this tab ----

    /// The search starts empty each time the tab shows.
    function leave() {
        root.search = ""
    }

    /// Freeform: a new, untitled item, open in the editor. A web page: its
    /// dialog, empty. Media: import files.
    function createItem() {
        if (!root.session)
            return
        if (root.isWeb) {
            root.webPageDialog().openNew()
            return
        }
        if (!root.isFreeform) {
            root.importDialog().open()
            return
        }
        const id = root.session.freeformLibrary.createFreeform(qsTr("Untitled"))
        if (id >= 0)
            root.session.slideEdit.openFreeform(id)
    }

    /// Copy media files ``paths`` (urls) into the folder showing, in the background.
    function importFiles(paths, folderId) {
        if (!root.session || root.isItemLibrary)
            return
        root.session.media.importMedia(root.kind, paths, folderId)
    }

    /// The probe the Convert dialog waits for, and the conversions it asked
    /// for whose errors this tab reports.
    property var convertProbeId: -1
    property var convertRequests: []

    /// Open Convert on the selected files, reading the first for its choices.
    function convertSelected() {
        if (!root.canConvert)
            return
        const media = root.session.media
        const files = root.selectedFilenames.map(fn => ({
            title: media.mediaFileDisplayName(root.kind, fn),
            filename: fn,
            // Its library thumbnail, until a video's probe brings a frame.
            thumb: media.mediaThumbUrl(root.kind, fn)
        }))
        root.convertDialog().openFor(root.isVideo ? "video" : root.isAudio ? "audio" : "image", files)
        root.convertProbeId = root.session.convert.probeMedia(root.kind, files[0].filename)
    }

    Connections {
        target: root.session ? root.session.convert : null
        function onProbeFinished(requestId, result) {
            if (requestId !== root.convertProbeId)
                return
            root.convertProbeId = -1
            root.convertDialog().setProbe(result, result.ok ? "" : result.error)
        }
        function onConvertFinished(requestId, result) {
            const at = root.convertRequests.indexOf(requestId)
            if (at < 0)
                return
            root.convertRequests = root.convertRequests.filter(id => id !== requestId)
            if (!result.ok)
                root.reportActionError(qsTr("Convert"), result.errors.join("\n"))
        }
    }

    function createChosen(id) {
        if (!root.session)
            return
        if (id === "new")
            root.createItem()
        else if (id === "importFile")
            root.freeformImportDialog().start()
        else if (id === "computer")
            root.importDialog().open()
        else if (id === "internet")
            root.downloadDialog().openDialog(root.isAudio)
    }

    /// The selected keys of ``kind`` (see keysOfKind), in the order the
    /// browser shows them.
    function keysInListOrder(kind) {
        const rowOf = key => root.entrySelection.rowOfKey(kind + ":" + key)
        return root.keysOfKind(kind).sort((a, b) => rowOf(a) - rowOf(b))
    }

    /// Freeform: the selected items and folders as images, or as a Freeform
    /// file. Media: the selected files, and folders with all in them, copied
    /// out to a folder.
    function exportChosen(id) {
        if (!root.canExport || !root.session)
            return
        const exporter = root.session.exporter
        const folders = root.keysInListOrder("folder").map(Number)
        if (id === "images" && root.isFreeform) {
            const items = root.keysInListOrder("item").map(Number)
            const single = items.length === 1 && !folders.length
            const name = (single && root.itemName(items[0])) || qsTr("Freeform")
            const what = single
                         ? qsTr("\u201c%1\u201d, a PNG image per slide, in a ZIP file.").arg(name)
                         : qsTr("The selected Freeform items, a PNG image per slide, in one ZIP file.")
            root.exportImages(what, name, (outputId, file) =>
                              exporter.exportFreeformImages(items, folders, outputId, file))
        } else if (id === "file" && root.isFreeform) {
            const items = root.keysInListOrder("item").map(Number)
            const single = items.length === 1 && !folders.length
            const oneFolder = !items.length && folders.length === 1
            const name = (single && root.itemName(items[0]))
                         || (oneFolder && root.folderName(folders[0])) || qsTr("Freeform")
            const suffix = exporter.freeformFileExtension(items.length, folders.length > 0)
            const filter = qsTr("Freeform files (*%1)").arg(suffix)
            root.exportToFile(qsTr("Export as a Freeform file"), filter, suffix.slice(1),
                              exporter.suggestedFileWith(name, suffix),
                              file => exporter.exportFreeformFile(items, folders, file))
        } else if (id === "files" && !root.isItemLibrary) {
            const files = root.keysInListOrder("file")
            root.exportToFolder(qsTr("Export to a folder"), folder =>
                                exporter.exportMediaFiles(root.kind, files, folders, folder))
        }
    }

    /// The downloads this tab started, whose errors it reports.
    property var downloadRequests: []

    Connections {
        target: root.session ? root.session.download : null
        function onDownloadFinished(requestId, result) {
            if (root.downloadRequests.indexOf(requestId) < 0)
                return
            root.downloadRequests = root.downloadRequests.filter(id => id !== requestId)
            if (!result.ok)
                root.reportActionError(qsTr("Download"), result.errors.join("\n"))
        }
    }

    /// Open the selected Freeform item in its editor, or a web page in its
    /// dialog; rename anything else.
    function editSelected() {
        if (!root.canEdit)
            return
        if (root.isItemLibrary && root.selectedFolderId < 0 && root.selectedItemId >= 0)
            root.editItem(root.selectedItemId)
        else
            root.promptRename()
    }

    function editItem(id) {
        if (root.isFreeform)
            root.session.slideEdit.openFreeform(id)
        else if (root.isWeb)
            root.webPageDialog().openFor(root.session.webLibrary.page(id))
    }

    function duplicateSelected() {
        if (!root.canDuplicate || !root.session)
            return
        const ids = root.itemsToMove()
        for (let i = 0; i < ids.length; ++i)
            root.duplicateItem(ids[i])
    }

    // Duplicating and deleting a row are each library's own, as it keys its
    // rows. Typed, so qmllint checks the calls.
    /// Duplicate item ``id`` of the item library (Freeform, web pages).
    function duplicateItem(id: var) {
        if (root.isWeb)
            root.session.webLibrary.duplicateItem(id)
        else
            root.session.freeformLibrary.duplicateItem(id)
    }
    /// Delete one row, keyed as its library keys it: a file by name, an
    /// item by id. Empty on success, else why not.
    function deleteItem(key: var): string {
        if (root.isWeb)
            return root.session.webLibrary.deleteItem(key)
        if (root.isFreeform)
            return root.session.freeformLibrary.deleteItem(key)
        return root.session.media.library(root.kind).deleteItem(key)
    }

    /// What the delete confirm asks, and what it does to files still on their
    /// way here.
    function deleteText() {
        const n = root.selectionCount
        let text
        if (n > 1)
            text = root.foldersToMove().length
                   ? qsTr("Delete the %1 selected items? Folders and their contents will be removed.").arg(n)
                   : qsTr("Permanently delete the %1 selected items from the library?").arg(n)
        else if (root.foldersToMove().length)
            text = root.kindText.deleteFolder
        else
            text = root.kindText.deleteOne
        if (root.isItemLibrary || !root.session)
            return text
        const note = root.session.media.arrivingDeleteNote(root.kind, root.filesToMove(), root.foldersToMove())
        return note.length ? text + " " + note : text
    }

    /// Delete what is selected. Stops at the first refusal and returns it;
    /// "" when everything went.
    function deleteSelected() {
        if (!root.canDelete || !root.session)
            return ""
        const doomed = root.isItemLibrary ? root.itemsToMove() : root.filesToMove()
        for (let i = 0; i < doomed.length; ++i) {
            const err = root.deleteItem(doomed[i])
            if (err.length)
                return err
        }
        const folders = root.foldersToMove()
        for (let i = 0; i < folders.length; ++i) {
            const err = root.library.deleteFolder(folders[i])
            if (err.length)
                return err
        }
        root.clearSelection()
        return ""
    }

    function enterFolder(id) {
        if (!root.library || id < 0)
            return
        root.library.setFolder(id)
        root.clearSelection()
    }

    /// Up a folder: Backspace, Alt+Up (Windows Explorer's), or Ctrl+Up
    /// (Command+Up, the Finder's).
    function isUpKey(event) {
        return event.key === Qt.Key_Backspace
               || (event.key === Qt.Key_Up && (event.modifiers & (Qt.AltModifier | Qt.ControlModifier)))
    }

    /// Add to the service: Insert, or Ctrl+Return (Command+Return) where
    /// there is no Insert key, as on a Mac's.
    function isAddKey(event) {
        return event.key === Qt.Key_Insert
               || ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                   && (event.modifiers & Qt.ControlModifier))
    }

    /// The list and icon views' keys: isUpKey goes up, isAddKey adds to the
    /// service, Return opens or plays the selection.
    function handleBrowserKey(event) {
        if (root.isUpKey(event)) {
            root.goUp()
            event.accepted = true
            return
        }
        if (root.isAddKey(event)) {
            root.addToService()
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (root.selectedParent)
                root.goUp()
            else if (root.selectedFolderId >= 0)
                root.enterFolder(root.selectedFolderId)
            else if (root.isItemLibrary && root.selectedItemId >= 0)
                root.activateEntry("file", root.selectedItemId, "")
            else if (root.selectedFilename.length)
                root.activateEntry("file", -1, root.selectedFilename)
            event.accepted = true
        }
    }

    function goUp() {
        if (!root.library || root.atRoot)
            return
        root.library.folderUp()
        root.clearSelection()
    }

    /// Open a folder, or send a file to its stack (cued where the stack has
    /// a Preview). With Shift: a file goes straight live, and an image folder
    /// is sent as a whole rather than opened.
    function activateEntry(kind, entryId, filename, shiftHeld) {
        if (kind === "parent") {
            root.goUp()
            return
        }
        if (kind === "folder") {
            if (shiftHeld && root.isImage && root.session) {
                root.session.media.playImageFolder(entryId, 0)
                return
            }
            root.enterFolder(entryId)
            return
        }
        if (kind === "file") {
            const live = !!shiftHeld
            if (root.isFreeform && entryId >= 0)
                root.session.live.playFreeform(entryId, live)
            else if (root.isWeb && entryId >= 0)
                root.session.webLibrary.playPage(entryId, live)
            else if (filename && filename.length) {
                if (root.isImage)
                    root.session.live.playImage(filename, live)
                else if (root.isVideo)
                    root.session.live.playVideo(filename, false, live)
                else if (root.isAudio)
                    root.session.live.playAudio(filename, false, live)
            }
        }
    }

    function toggleView() {
        if (!root.session)
            return
        root.session.media.mediaViewMode = root.iconsMode ? "list" : "icons"
    }

    function newFolder() {
        folderNameMode = "create"
        root.renameLockedExt = ""
        folderNameDialog.ask(qsTr("New folder"))
    }

    function promptRenameFolder() {
        if (root.selectedFolderId < 0 || !root.session)
            return
        let name = qsTr("Folder")
        const flat = root.library.listFoldersFlat()
        for (let i = 0; i < flat.length; ++i) {
            if (flat[i].id === root.selectedFolderId) {
                name = flat[i].name
                break
            }
        }
        root.renameLockedExt = ""
        folderNameMode = "rename"
        folderNameDialog.ask(name)
    }

    /// The name the browser shows for item ``id`` of the item library.
    function folderName(id) {
        const row = root.entrySelection.rowOfKey(root.entryKey("folder", id, ""))
        const name = row >= 0 ? MultiSelect.modelValue(root.browserModel, row, MediaBrowserModel.NameRole)
                              : ""
        return String(name || "")
    }

    function itemName(id) {
        const row = root.entrySelection.rowOfKey(root.entryKey("file", id, ""))
        const name = row >= 0 ? MultiSelect.modelValue(root.browserModel, row, MediaBrowserModel.NameRole)
                              : ""
        return String(name || "")
    }

    /// What sync knows of ``filename`` while it is not here
    /// (``MediaController::arrival``); empty for what is, and for the item
    /// libraries.
    function arrivalOf(filename) {
        if (!root.session || root.isItemLibrary || !filename || !filename.length)
            return ({})
        return root.session.media.arrival(root.kind, filename, root.session.media.arrivalsRevision)
    }

    function displayNameForFile(filename) {
        if (!filename || !filename.length)
            return ""
        if (root.session)
            return root.session.media.mediaFileDisplayName(root.kind, filename) || filename
        return filename
    }

    function splitNameExt(name) {
        const s = String(name || "")
        const dot = s.lastIndexOf(".")
        if (dot <= 0)
            return { stem: s, ext: "" }
        return { stem: s.substring(0, dot), ext: s.substring(dot) }
    }

    function promptRenameFile() {
        if (!root.selectedFilename.length || !root.session)
            return
        const display = root.displayNameForFile(root.selectedFilename)
        const parts = root.splitNameExt(display)
        // Prefer on-disk extension when display has none (rare).
        const diskParts = root.splitNameExt(root.selectedFilename)
        root.renameLockedExt = parts.ext.length ? parts.ext : diskParts.ext
        if (root.isAudio) {
            // An audio file's name, and the fades it sets for itself.
            root.audioFileDialog().openFor(root.selectedFilename, parts.stem, root.renameLockedExt,
                                           root.session.media.audioFileFades(root.selectedFilename))
            return
        }
        folderNameMode = "renameFile"
        folderNameDialog.ask(parts.stem)
    }

    /// Keep what the audio file dialog says of ``filename``: its title, if it
    /// changed, and its fades.
    function saveAudioFile(filename, stem, fades) {
        const parts = root.splitNameExt(root.displayNameForFile(filename))
        if (stem !== parts.stem) {
            const result = root.session.media.renameMediaFile(root.kind, filename, stem)
            if (root.reportActionError(qsTr("Could not rename"), result ? result.error : ""))
                return
        }
        root.reportActionError(qsTr("Could not save the fades"),
                               root.session.media.setAudioFileFades(filename, fades))
    }

    function promptRename() {
        if (root.selectedFolderId >= 0)
            root.promptRenameFolder()
        else if (root.isItemLibrary)
            return
        else if (root.selectedFilename.length)
            root.promptRenameFile()
    }

    /// Show ``err`` (an entry point's returned message) under ``title``; true
    /// when there was one. The prompt that asked has already closed.
    function reportActionError(title, err) {
        if (!err || !err.length)
            return false
        actionError.title = title
        actionError.ask(err)
        return true
    }

    function submitFolderName() {
        if (!root.session)
            return
        const name = folderNameDialog.trimmedValue
        if (!name.length)
            return
        if (folderNameMode === "rename") {
            const err = root.library.renameFolder(root.selectedFolderId, name)
            root.reportActionError(qsTr("Could not rename folder"), err)
            return
        }
        if (folderNameMode === "renameFile") {
            const result = root.session.media.renameMediaFile(root.kind, root.selectedFilename, name)
            if (root.reportActionError(qsTr("Could not rename"), result ? result.error : ""))
                return
            const next = (result && result.filename) ? result.filename : root.selectedFilename
            if (next && next.length)
                root.selectFile(next, false)
            return
        }
        const id = root.library.createFolder(name)
        if (id >= 0)
            root.selectFolder(id, false)
    }

    function filesToMove() {
        if (root.selectedFilenames.length)
            return root.selectedFilenames.slice()
        if (root.selectedFilename.length)
            return [root.selectedFilename]
        return []
    }

    function foldersToMove() {
        if (root.selectedFolderIds.length)
            return root.selectedFolderIds.slice()
        if (root.selectedFolderId >= 0)
            return [root.selectedFolderId]
        return []
    }

    function itemsToMove() {
        if (root.selectedItemIds.length)
            return root.selectedItemIds.slice()
        if (root.selectedItemId >= 0)
            return [root.selectedItemId]
        return []
    }

    function fileIdsForFilenames(list) {
        const fileIds = []
        const model = root.browserModel
        if (!model || !list || !list.length)
            return fileIds
        const selected = {}
        for (let i = 0; i < list.length; ++i)
            selected[String(list[i])] = true
        const rows = model.rowCount()
        for (let i = 0; i < rows; ++i) {
            const e = root.browserEntry(i)
            if (e.kind === "file" && selected[e.filename] && e.entryId >= 0)
                fileIds.push(e.entryId)
        }
        return fileIds
    }

    function beginDragFrom(kind, entryId, filename) {
        root.dropApplied = false
        root.dropHighlightId = null
        root.dropInsertKey = ""
        if (kind === "file") {
            if (root.isItemLibrary) {
                if (!root.isItemSelected(entryId))
                    root.selectItem(entryId, false)
            } else if (!root.isFileSelected(filename)) {
                root.selectFile(filename, false)
            }
        } else if (kind === "folder") {
            if (!root.isFolderSelected(entryId))
                root.selectFolder(entryId, false)
        }
        const folders = root.foldersToMove()
        if (root.isItemLibrary) {
            const ids = root.itemsToMove()
            root.pendingMove = {
                files: [],
                folders: folders.slice(),
                fileIds: ids.length ? ids : (kind === "file" ? [entryId] : [])
            }
        } else {
            const files = root.filesToMove()
            const list = files.length ? files : (kind === "file" && filename ? [filename] : [])
            root.pendingMove = {
                files: list,
                folders: folders.length ? folders : (kind === "folder" ? [entryId] : []),
                fileIds: root.fileIdsForFilenames(list)
            }
        }
    }

    function orderedServiceItems() {
        const items = []
        const model = root.browserModel
        if (!model)
            return items
        for (let i = 0; i < model.rowCount(); ++i) {
            const e = root.browserEntry(i)
            if (!e || e.kind === "parent")
                continue
            if (e.kind === "folder" && root.isFolderSelected(e.entryId)) {
                items.push({ kind: root.kindText.serviceFolder, folderIds: [e.entryId] })
            } else if (e.kind === "file") {
                if (root.isFreeform && root.isItemSelected(e.entryId))
                    items.push({ kind: "freeform", freeformId: e.entryId })
                else if (root.isWeb && root.isItemSelected(e.entryId))
                    items.push({ kind: "web", pageIds: [e.entryId] })
                else if (!root.isItemLibrary && root.isFileSelected(e.filename))
                    items.push({ kind: root.kindText.serviceItem, filenames: [e.filename] })
            }
        }
        return items
    }

    function addToService() {
        if (!root.canAdd)
            return
        const payload = root.serviceAddPayload()
        if (payload && !root.applyLibraryPayload(payload) && root.session && payload.kind === "batch") {
            const items = payload.items || []
            for (let i = 0; i < items.length; ++i)
                root.applyLibraryPayload(items[i])
        }
    }

    function serviceAddPayload() {
        const items = root.orderedServiceItems()
        if (items.length === 1)
            return items[0]
        if (items.length > 1)
            return { kind: "batch", items: items }
        return null
    }

    function serviceAddLabel(payload) {
        const n = root.selectionCount
        if (n > 1)
            return qsTr("%1 items").arg(n)
        if (!payload)
            return qsTr("Add to service")
        if (payload.filenames && payload.filenames.length)
            return payload.filenames[0]
        if (payload.kind === "freeform")
            return qsTr("Freeform")
        if (payload.folderIds)
            return qsTr("Folder")
        return qsTr("Add to service")
    }

    /// Arm library -> service drop only while the pointer is over the service
    /// list (or collapsed header). Clicks and in-library reorders must not
    /// flash a service ghost.
    function hoverServiceAddFrom(mapItem, mx, my, label) {
        if (!root.serviceHost || !root.session || root.session.openServiceId < 0)
            return false
        const at = root.serviceHost.serviceDropIndexAt
                 ? root.serviceHost.serviceDropIndexAt(mapItem, mx, my) : -1
        if (at < 0) {
            if (root.serviceHost.libAddDragActive)
                root.serviceHost.cancelLibAddDrag()
            return false
        }
        const payload = root.serviceAddPayload()
        if (!payload)
            return false
        const text = (label && String(label).length) ? label
                   : (root.dragGhostLabel.length ? root.dragGhostLabel
                      : root.serviceAddLabel(payload))
        if (!root.serviceHost.libAddDragActive)
            root.serviceHost.beginLibAddDrag(payload, text, 16, 16)
        else {
            root.serviceHost.libAddPayload = payload
            root.serviceHost.libAddLabel = text
        }
        root.serviceHost.updateLibAddDragFrom(mapItem, mx, my)
        return true
    }

    function clearDrag() {
        root.pendingMove = null
        root.dropHighlightId = null
        root.dropInsertKey = ""
        root.dragging = false
        root.dragGhostLabel = ""
        root.dropApplied = false
        root.dragScrollDir = 0
        if (root.serviceHost && root.serviceHost.libAddDragActive)
            root.serviceHost.cancelLibAddDrag()
        if (root.marqueeActive)
            root.endMarquee()
    }

    function browserScrollView() {
        return root.iconsMode ? iconFlick : listView
    }

    function isOverEntryAt(mapItem, mx, my) {
        if (root.iconsMode) {
            if (!iconFlick || !iconFlow)
                return false
            const p = mapItem.mapToItem(iconFlow, mx, my)
            const kids = iconFlow.children
            for (let i = 0; i < kids.length; ++i) {
                const t = kids[i]
                if (!t || t.kind === undefined)
                    continue
                if (p.x >= t.x && p.x < t.x + t.width
                        && p.y >= t.y && p.y < t.y + t.height)
                    return true
            }
            return false
        }
        if (!listView)
            return false
        const lp = mapItem.mapToItem(listView, mx, my)
        return MultiSelect.isOverListRow(listView, lp.x, lp.y)
    }

    function captureBrowserScroll() {
        const view = root.browserScrollView()
        if (!view)
            return
        root.restoreContentY = view.contentY
    }

    function applyRestoredBrowserScroll() {
        if (root.restoreContentY < 0)
            return
        const view = root.browserScrollView()
        if (!view)
            return
        const y = root.restoreContentY
        const maxY = Math.max(0, view.contentHeight - view.height)
        view.contentY = Math.max(0, Math.min(maxY, y))
    }

    function releaseBrowserScrollCapture() {
        // Keep restoreContentY armed briefly so contentHeight settling
        // after beginResetModel can re-clamp without jumping to top.
        restoreScrollClear.restart()
    }

    function updateDragScrollFromRoot() {
        const view = root.browserScrollView()
        if (!view || !root.dragging) {
            root.dragScrollDir = 0
            return
        }
        const p = root.mapToItem(view, root.dragPointerRootX, root.dragPointerRootY)
        if (p.x < -8 || p.x > view.width + 8 || p.y < -8 || p.y > view.height + 8) {
            root.dragScrollDir = 0
            return
        }
        const prev = root.dragScrollDir
        if (p.y < root.dragScrollEdge)
            root.dragScrollDir = -1
        else if (p.y > view.height - root.dragScrollEdge)
            root.dragScrollDir = 1
        else
            root.dragScrollDir = 0
        // First step immediately when entering an edge zone.
        if (root.dragScrollDir !== 0 && prev === 0)
            root.applyDragScrollStep()
    }

    function applyDragScrollStep() {
        if (!root.dragging || root.dragScrollDir === 0)
            return
        const view = root.browserScrollView()
        if (!view)
            return
        const maxY = Math.max(0, view.contentHeight - view.height)
        if (maxY <= 0)
            return
        const next = view.contentY + root.dragScrollDir * root.dragScrollStep
        const clamped = Math.max(0, Math.min(maxY, next))
        if (Math.abs(clamped - view.contentY) < 0.5)
            return
        view.contentY = clamped
        // Keep drop markers current under a held cursor while content moves.
        root.refreshDragHover()
    }

    function refreshDragHover() {
        if (!root.pendingMove || !root.dragging)
            return
        const mx = root.dragPointerRootX
        const my = root.dragPointerRootY
        root.ghostX = mx - dragGhost.width / 2
        root.ghostY = my - dragGhost.height / 2

        if (root.hoverServiceAddFrom(root, mx, my, root.dragGhostLabel)) {
            root.dropHighlightId = null
            root.dropInsertKey = ""
            root.ghostX = -10000
            root.ghostY = -10000
            root.dragScrollDir = 0
            return
        }
        root.updateDropHighlightAt(root, mx, my)
    }

    function updateDropHighlightAt(mapItem, mx, my) {
        const hit = root.hitTestDropTarget(mapItem, mx, my)
        if (!hit) {
            root.dropHighlightId = null
            root.dropInsertKey = ""
            return
        }
        const draggingFolders = !!(root.pendingMove.folders && root.pendingMove.folders.length)
        const draggingFiles = !!(root.pendingMove.files && root.pendingMove.files.length)
                           || !!(root.pendingMove.fileIds && root.pendingMove.fileIds.length)
        if (hit.kind === "parent") {
            root.dropHighlightId = "parent"
            root.dropInsertKey = ""
            return
        }
        // Nest into folder when dropping on its body; edge = sibling reorder.
        if (hit.kind === "folder" && hit.edge === "on" && !draggingFolders) {
            root.dropHighlightId = hit.entryId
            root.dropInsertKey = ""
            return
        }
        if (draggingFolders && hit.kind === "folder") {
            if (hit.edge === "on") {
                root.dropHighlightId = hit.entryId
                root.dropInsertKey = ""
            } else {
                root.dropHighlightId = null
                root.dropInsertKey = "folder:" + hit.entryId + ":" + hit.edge
            }
            return
        }
        if (draggingFiles && hit.kind === "file"
                && (root.isItemLibrary || root.currentFolderId >= 0)) {
            root.dropHighlightId = null
            root.dropInsertKey = "file:" + hit.entryId + ":"
                    + (hit.edge === "after" ? "after" : "before")
            return
        }
        if (hit.kind === "folder") {
            root.dropHighlightId = hit.entryId
            root.dropInsertKey = ""
            return
        }
        root.dropHighlightId = null
        root.dropInsertKey = ""
    }

    /// The entry among ``items`` (tiles or list rows) at ``p``, in their
    /// parent's coordinates, with where on it a drop lands: "before", "on"
    /// or "after". Null over none.
    function entryAt(items, p) {
        for (let i = 0; i < items.length; ++i) {
            const c = items[i]
            if (!c || !c.visible || c.width <= 0 || c.height <= 0)
                continue
            if (c.kind === undefined)
                continue
            if (p.x >= c.x && p.x < c.x + c.width && p.y >= c.y && p.y < c.y + c.height) {
                const edge = (p.y - c.y) < c.height * 0.35 ? "before"
                             : ((p.y - c.y) > c.height * 0.65 ? "after" : "on")
                return { kind: c.kind, entryId: c.entryId, filename: c.filename || "",
                         edge: edge }
            }
        }
        return null
    }

    function hitTestDropTarget(mapItem, mx, my) {
        if (root.iconsMode)
            return root.entryAt(iconFlow.children, mapItem.mapToItem(iconFlow, mx, my))
        return root.entryAt(listView.contentItem.children,
                            mapItem.mapToItem(listView.contentItem, mx, my))
    }

    function updateDragAt(mapItem, mx, my, label) {
        if (!root.pendingMove)
            return
        if (!root.dragging) {
            root.dragging = true
            const n = root.selectedEntryCount
            root.dragGhostLabel = n > 1 ? qsTr("%1 items").arg(n) : label
        }
        const g = mapItem.mapToItem(root, mx, my)
        root.dragPointerRootX = g.x
        root.dragPointerRootY = g.y
        root.ghostX = g.x - dragGhost.width / 2
        root.ghostY = g.y - dragGhost.height / 2

        if (root.hoverServiceAddFrom(mapItem, mx, my, label)) {
            root.dropHighlightId = null
            root.dropInsertKey = ""
            root.dragScrollDir = 0
            // Hide local ghost while the service host ghost tracks the cursor.
            root.ghostX = -10000
            root.ghostY = -10000
            return
        }

        root.updateDragScrollFromRoot()
        root.updateDropHighlightAt(root, g.x, g.y)
    }

    function reorderIdsMoving(orderedIds, movingIds, targetId, edge) {
        const moving = {}
        for (let i = 0; i < movingIds.length; ++i)
            moving[Number(movingIds[i])] = true
        const rest = []
        for (let i = 0; i < orderedIds.length; ++i) {
            const id = Number(orderedIds[i])
            if (!moving[id])
                rest.push(id)
        }
        let insertAt = rest.indexOf(Number(targetId))
        if (insertAt < 0)
            return null
        if (edge === "after")
            insertAt += 1
        const moved = []
        for (let i = 0; i < orderedIds.length; ++i) {
            const id = Number(orderedIds[i])
            if (moving[id])
                moved.push(id)
        }
        if (!moved.length)
            return null
        return rest.slice(0, insertAt).concat(moved).concat(rest.slice(insertAt))
    }

    function applyReorderDrop(insertKey) {
        if (!root.session || !root.browserModel || !insertKey || !insertKey.length)
            return false
        const parts = String(insertKey).split(":")
        if (parts.length < 3)
            return false
        const kind = parts[0]
        const targetId = Number(parts[1])
        const edge = parts[2]
        if (isNaN(targetId))
            return false

        if (kind === "folder") {
            const raw = root.pendingMove.folders ? root.pendingMove.folders : []
            const moving = []
            for (let i = 0; i < raw.length; ++i) {
                const id = Number(raw[i])
                if (!isNaN(id))
                    moving.push(id)
            }
            const ordered = root.browserModel.entryIds("folder")
            const next = root.reorderIdsMoving(ordered, moving, targetId, edge)
            if (!next || next.length !== ordered.length)
                return false
            root.dropApplied = true
            root.captureBrowserScroll()
            root.library.reorderFolders(root.currentFolderId, next)
            root.applyRestoredBrowserScroll()
            root.releaseBrowserScrollCapture()
            root.clearSelection()
            return true
        }
        if (kind === "file" && (root.isItemLibrary || root.currentFolderId >= 0)) {
            const movingIds = root.pendingMove.fileIds ? root.pendingMove.fileIds : []
            const ordered = root.browserModel.entryIds("file")
            const next = root.reorderIdsMoving(ordered, movingIds, targetId, edge)
            if (!next || next.length !== ordered.length)
                return false
            root.dropApplied = true
            root.captureBrowserScroll()
            root.library.reorderItems(root.currentFolderId, next)
            root.applyRestoredBrowserScroll()
            root.releaseBrowserScrollCapture()
            root.clearSelection()
            return true
        }
        return false
    }

    function applyDropOn(targetKind, targetEntryId) {
        if (!root.session || root.dropApplied || !root.pendingMove)
            return
        let dest = -1
        if (targetKind === "parent") {
            dest = root.library.folderParentId()
        } else if (targetKind === "folder") {
            dest = Number(targetEntryId)
            if (isNaN(dest))
                return
        } else {
            return
        }

        const rawFiles = root.pendingMove.files ? root.pendingMove.files : []
        const rawFileIds = root.pendingMove.fileIds ? root.pendingMove.fileIds : []
        const rawFolders = root.pendingMove.folders ? root.pendingMove.folders : []
        const files = []
        for (let i = 0; i < rawFiles.length; ++i)
            files.push(String(rawFiles[i]))
        const fileIds = []
        for (let i = 0; i < rawFileIds.length; ++i) {
            const id = Number(rawFileIds[i])
            if (!isNaN(id) && id >= 0)
                fileIds.push(id)
        }
        const folders = []
        for (let i = 0; i < rawFolders.length; ++i) {
            const id = Number(rawFolders[i])
            if (!isNaN(id))
                folders.push(id)
        }
        if (!files.length && !fileIds.length && !folders.length)
            return
        for (let i = 0; i < folders.length; ++i) {
            if (folders[i] === dest)
                return
        }

        root.dropApplied = true
        root.captureBrowserScroll()
        // Each library keys its rows its own way: media files by name.
        const items = root.isItemLibrary ? fileIds : files
        if (items.length)
            root.reportActionError(qsTr("Could not move"), root.library.moveItems(items, dest))
        for (let i = 0; i < folders.length; ++i)
            root.library.moveFolder(folders[i], dest)
        root.applyRestoredBrowserScroll()
        root.releaseBrowserScrollCapture()
        root.clearSelection()
    }

    function finishDrag(mapItem, mx, my) {
        root.captureBrowserScroll()
        if (!root.dragging) {
            root.clearDrag()
            root.releaseBrowserScrollCapture()
            return
        }
        // Library -> service takes priority when the drop is over the service list.
        if (root.serviceHost && root.serviceHost.libAddDragActive) {
            if (root.serviceHost.finishLibAddDragFrom(mapItem, mx, my)) {
                root.pendingMove = null
                root.dropHighlightId = null
                root.dropInsertKey = ""
                root.dragging = false
                root.dragGhostLabel = ""
                root.dropApplied = true
                root.dragScrollDir = 0
                root.applyRestoredBrowserScroll()
                root.releaseBrowserScrollCapture()
                return
            }
        }
        if (root.dropInsertKey && root.dropInsertKey.length) {
            if (root.applyReorderDrop(root.dropInsertKey)) {
                root.clearDrag()
                root.applyRestoredBrowserScroll()
                root.releaseBrowserScrollCapture()
                const v = root.browserScrollView()
                if (v)
                    v.cancelFlick()
                return
            }
        }
        const hit = root.hitTestDropTarget(mapItem, mx, my)
        if (hit && (hit.kind === "folder" || hit.kind === "parent"))
            root.applyDropOn(hit.kind, hit.entryId)
        else if (root.dropHighlightId === "parent")
            root.applyDropOn("parent", -1)
        else if (root.dropHighlightId !== null && root.dropHighlightId !== undefined)
            root.applyDropOn("folder", root.dropHighlightId)
        root.clearDrag()
        root.applyRestoredBrowserScroll()
        root.releaseBrowserScrollCapture()
        const view = root.browserScrollView()
        if (view)
            view.cancelFlick()
    }

    function dropTargetId(kind, entryId) {
        if (kind === "parent")
            return -1
        if (kind === "folder")
            return entryId
        return null
    }

    function insertMarkerFor(kind, entryId, edge) {
        if (!root.dropInsertKey.length)
            return false
        return root.dropInsertKey === (kind + ":" + entryId + ":" + edge)
    }

    Keys.onPressed: function (event) {
        if (root.isUpKey(event)) {
            root.goUp()
            event.accepted = true
        }
    }

    Timer {
        id: dragScrollTimer
        interval: 16
        repeat: true
        running: root.dragging && root.dragScrollDir !== 0
        onTriggered: root.applyDragScrollStep()
    }

    Timer {
        id: restoreScrollClear
        interval: 80
        onTriggered: root.restoreContentY = -1
    }

    Connections {
        target: listView
        enabled: !root.iconsMode && root.restoreContentY >= 0
        function onContentHeightChanged() { root.applyRestoredBrowserScroll() }
        function onHeightChanged() { root.applyRestoredBrowserScroll() }
    }

    Connections {
        target: iconFlick
        enabled: root.iconsMode && root.restoreContentY >= 0
        function onContentHeightChanged() { root.applyRestoredBrowserScroll() }
        function onHeightChanged() { root.applyRestoredBrowserScroll() }
    }

    Rectangle {
        id: dragGhost
        visible: root.dragging
        enabled: false
        z: 10000
        x: root.ghostX
        y: root.ghostY
        width: Math.min(root.iconsMode ? root.iconTileW : 200, Math.max(80, root.width - 16))
        height: root.iconsMode ? 36 : root.rowHeight
        radius: Style.radiusSmall
        color: Style.dragGhostBg
        border.width: 1
        border.color: Style.listAccentSel
        opacity: 0.95
        Text {
            anchors.fill: parent
            anchors.margins: 8
            verticalAlignment: Text.AlignVCenter
            horizontalAlignment: root.iconsMode ? Text.AlignHCenter : Text.AlignLeft
            text: root.dragGhostLabel
            color: Style.text
            font.pixelSize: Style.fontBody
            elide: Text.ElideRight
        }
    }

    Item {
        anchors.fill: parent
        clip: true

        ListView {
            id: listView
            anchors.fill: parent
            visible: !root.iconsMode
            clip: true
            model: root.browserModel
            spacing: root.listGap
            focus: true
            interactive: !root.dragging && !root.marqueeActive
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}
            Keys.onPressed: function (event) { root.handleBrowserKey(event) }
            delegate: MediaBrowserRow {
                browser: root
            }
            EmptyNote {
                visible: root.showsNothing
                text: root.emptyText
            }

            MarqueeRect {
                view: listView
                active: root.marqueeActive && !root.iconsMode
                contentX1: root.entrySelection.marqueeX1
                contentY1: root.entrySelection.marqueeY1
                contentX2: root.entrySelection.marqueeX2
                contentY2: root.entrySelection.marqueeY2
            }
        }

        Flickable {
            id: iconFlick
            anchors.fill: parent
            visible: root.iconsMode
            clip: true
            contentWidth: width - Style.scrollGutter(iconFlick)
            contentHeight: iconFlow.implicitHeight
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            interactive: !root.dragging && !root.marqueeActive
            ScrollBar.vertical: AppScrollBar {}
            focus: true
            Keys.onPressed: function (event) { root.handleBrowserKey(event) }
            Grid {
                id: iconFlow
                width: iconFlick.width - Style.scrollGutter(iconFlick)
                // Column count from the full viewport so reserving a gutter
                // only narrows cells - it cannot wrap 4 columns down to 3.
                readonly property int layoutW: iconFlick.width
                columns: Math.max(1, Math.floor(
                             (layoutW + root.iconGap)
                             / (root.iconMinTileW + root.iconGap)))
                columnSpacing: root.iconGap
                rowSpacing: root.iconGap
                readonly property real cellW: {
                    const cols = Math.max(1, columns)
                    return Math.max(1, (width - root.iconGap * (cols - 1)) / cols)
                }
                readonly property int previewH: {
                    const cols = Math.max(1, columns)
                    const fullCellW = Math.max(1, (layoutW - root.iconGap * (cols - 1)) / cols)
                    return Math.round(fullCellW * 9 / 16)
                }
                Repeater {
                    id: iconRepeater
                    // A Repeater makes every tile whether shown or not, so
                    // the list view must not leave it a model.
                    model: root.iconsMode ? root.browserModel : null
                    delegate: MediaBrowserTile {
                        browser: root
                        cellW: iconFlow.cellW
                        previewH: iconFlow.previewH
                        focusTarget: iconFlick
                    }
                }
            }
            EmptyNote {
                visible: root.showsNothing
                text: root.emptyText
            }

            MarqueeRect {
                view: iconFlick
                active: root.marqueeActive && root.iconsMode
                contentX1: root.entrySelection.marqueeX1
                contentY1: root.entrySelection.marqueeY1
                contentX2: root.entrySelection.marqueeX2
                contentY2: root.entrySelection.marqueeY2
            }
        }
    }

    AppPromptDialog {
        id: folderNameDialog
        title: {
            if (root.folderNameMode === "renameFile")
                return root.kindText.renameTitle
            if (root.folderNameMode === "rename")
                return qsTr("Rename folder")
            return qsTr("New folder")
        }
        acceptLabel: root.folderNameMode === "create" ? qsTr("Create") : qsTr("Rename")
        suffix: root.folderNameMode === "renameFile" ? root.renameLockedExt : ""
        onAccepted: root.submitFolderName()
    }

    AppDialog {
        id: actionError
        acknowledge: true
    }

    // The kind's dialogs, each made the first time it is asked for.
    function downloadDialog(): DownloadDialog {
        return downloadLoader.load() as DownloadDialog
    }
    function webPageDialog(): WebPageDialog {
        return webPageLoader.load() as WebPageDialog
    }
    function convertDialog(): ConvertDialog {
        return convertLoader.load() as ConvertDialog
    }
    function importDialog(): FileDialog {
        return importLoader.load() as FileDialog
    }
    function freeformImportDialog(): FreeformImportDialog {
        return freeformImportLoader.load() as FreeformImportDialog
    }
    function audioFileDialog(): AudioFileDialog {
        return audioFileLoader.load() as AudioFileDialog
    }

    // Import a Freeform file or a service's package.
    LazyLoader {
        id: freeformImportLoader
        objectName: "FreeformImportDialog"
        sourceComponent: FreeformImportDialog {
            id: freeformImport
            session: root.session
            onFailed: function (why) { root.reportActionError(qsTr("Could not import"), why) }
            onImported: function (result) { root.tellDone(qsTr("Imported"), freeformImport.describe(result)) }
        }
    }

    // An audio file's Edit: its name and its own fades.
    LazyLoader {
        id: audioFileLoader
        objectName: "AudioFileDialog"
        sourceComponent: AudioFileDialog {
            id: audioFile
            onSaved: function (stem, fades) { root.saveAudioFile(audioFile.filename, stem, fades) }
        }
    }

    LazyLoader {
        id: downloadLoader
        objectName: "DownloadDialog"
        sourceComponent: DownloadDialog {
            session: root.session
            videoFolderId: root.session ? root.session.media.videoFolderId : -1
            audioFolderId: root.session ? root.session.media.audioFolderId : -1
            onStarted: function (requestId) {
                root.downloadRequests = root.downloadRequests.concat([requestId])
            }
        }
    }

    // A web page's name, address, size and zoom (the Browser tab).
    LazyLoader {
        id: webPageLoader
        objectName: "WebPageDialog"
        sourceComponent: WebPageDialog {
            session: root.session
            onSaved: function (result) {
                if (!root.reportActionError(qsTr("Could not save the page"), result.error))
                    root.selectItem(result.id, false)
            }
        }
    }

    // Convert's choices; the conversions run in session.convert, and show in
    // the bottom bar.
    LazyLoader {
        id: convertLoader
        objectName: "ConvertDialog"
        sourceComponent: ConvertDialog {
            id: convert
            onConvertRequested: function (choice, replaceOriginal, name, outKind) {
                // Sound made from a video goes to the folder Audio is showing.
                const folder = outKind === "audio" && !root.isAudio ? root.session.media.audioFolderId
                                                                     : root.currentFolderId
                const id = root.session.convert.convertMedia(root.kind,
                                                             convert.sources.map(f => f.filename),
                                                             choice, folder, replaceOriginal, name)
                root.convertRequests = root.convertRequests.concat([id])
            }
        }
    }

    LazyLoader {
        id: importLoader
        objectName: "FileDialog"
        sourceComponent: FileDialog {
            title: root.kindText.importTitle ?? ""
            fileMode: FileDialog.OpenFiles
            nameFilters: root.isItemLibrary || !root.session
                         ? []
                         : ["%1 (%2)".arg(root.kindText.filterLabel)
                                    .arg(root.session.media.mediaFilePatterns(root.kind)),
                            "All files (*)"]
            onAccepted: root.importFiles(selectedFiles.map(function (u) { return u.toString() }),
                                         root.currentFolderId)
        }
    }

    // Empty space draws a marquee.
    MouseArea {
        anchors.fill: parent
        anchors.margins: -root.marqueeBleed
        z: 20
        hoverEnabled: false
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        onPressed: function (mouse) {
            const view = root.browserScrollView()
            if (view) {
                const p = mapToItem(view, mouse.x, mouse.y)
                if (p.x > view.width - Style.scrollGutter(view)
                        && p.x <= view.width
                        && p.y >= 0 && p.y <= view.height) {
                    mouse.accepted = false
                    return
                }
            }
            if (root.isOverEntryAt(this, mouse.x, mouse.y)) {
                mouse.accepted = false
                return
            }
            root.interacted()
            const toggle = MultiSelect.isToggle(mouse.modifiers)
            if (!toggle)
                root.clearSelection()
            root.beginMarqueeFrom(this, mouse.x, mouse.y, toggle)
        }
        onPositionChanged: function (mouse) {
            if (!pressed || !root.marqueeActive)
                return
            root.updateMarqueeFrom(this, mouse.x, mouse.y)
        }
        onReleased: function (mouse) {
            if (root.marqueeActive) {
                root.updateMarqueeFrom(this, mouse.x, mouse.y)
                root.endMarquee()
            }
        }
        onCanceled: root.endMarquee()
        onWheel: function (wheel) { wheel.accepted = false }
    }
}
