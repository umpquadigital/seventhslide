pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect

// The library's Songs tab: the song list, searched by title, author,
// songbook and number, topic, key or lyric, selected with the usual list
// rules and dragged (or added) onto the service.
LibraryTab {
    id: tab

    /// Save the song ``songId`` as a new Freeform item named ``name``.
    signal saveAsFreeformRequested(int songId, string name)

    searchPlaceholder: qsTr("Search Songs…")
    onSearchChanged: {
        if (tab.session)
            tab.session.library.songs.filter = tab.search
    }

    offersCreate: true
    // One button for both ways a song arrives: written here, or imported.
    createChoices: [{ id: "new", label: qsTr("New song…"), icon: "pen" },
                    { id: "computer", label: qsTr("From this computer…"), icon: "import" }]
    // Songs leave as files other programs read, or as their slides.
    exportChoices: [{ id: "xml", label: qsTr("As song files (OpenLyrics XML)…"), icon: "song" },
                    { id: "images", label: qsTr("As images…"), icon: "image" }]
    offersEdit: true
    offersConvert: true
    offersDelete: true
    addableSelectionCount: tab.selectedSongIds.length
    canEdit: tab.selectedSongIds.length === 1
    canConvert: tab.selectedSongIds.length === 1
    canDelete: tab.selectedSongIds.length > 0
    canExport: tab.selectedSongIds.length > 0

    /// The selected songs (by song id), and the one last picked.
    readonly property ListSelection songSelection: ListSelection {
        model: tab.session ? tab.session.library.songs : null
        keyRole: SongListModel.SongIdRole
        view: songList
        onChanged: tab.syncSongTitle()
    }
    readonly property var selectedSongIds: tab.songSelection.keys
    readonly property var selectedSongId: tab.songSelection.current !== null
                                          ? tab.songSelection.current : -1
    /// The current song's title; kept while a search hides its row.
    property string selectedSongTitle: ""

    // Row rhythm.
    readonly property int rowGap: 3
    readonly property int rowPadH: 10
    readonly property int rowPadV: 18

    function clearSelection() {
        tab.songSelection.clear()
        tab.songSelection.endMarquee()
    }

    function createChosen(id) {
        if (id === "new")
            tab.songEditor().openNewLibrary()
        else if (id === "computer")
            songDialog.open()
    }

    function editSelected() {
        if (tab.canEdit)
            tab.songEditor().openLibrary(tab.selectedSongId)
    }

    function convertSelected() {
        if (!tab.canConvert)
            return
        const name = tab.selectedSongTitle.length
                     ? (tab.selectedSongTitle + qsTr(" (edit)"))
                     : qsTr("Untitled")
        tab.saveAsFreeformRequested(tab.selectedSongId, name)
    }

    /// The selected songs in the order the list shows them.
    function songsInListOrder() {
        return tab.selectedSongIds.slice().sort((a, b) => tab.songSelection.rowOfKey(a)
                                                         - tab.songSelection.rowOfKey(b))
    }

    function exportChosen(id) {
        if (!tab.canExport)
            return
        const ids = tab.songsInListOrder()
        const exporter = tab.session.exporter
        if (id === "xml") {
            tab.exportToFolder(ids.length > 1 ? qsTr("Export %1 songs to a folder").arg(ids.length)
                                              : qsTr("Export the song to a folder"),
                               folder => exporter.exportSongXml(ids, folder))
        } else if (id === "images") {
            const what = ids.length > 1
                         ? qsTr("The %1 selected songs, a PNG image per slide, in one ZIP file.").arg(ids.length)
                         : qsTr("\u201c%1\u201d, a PNG image per slide, in a ZIP file.").arg(tab.selectedSongTitle)
            tab.exportImages(what, ids.length > 1 ? qsTr("Songs") : tab.selectedSongTitle,
                             (outputId, file) => exporter.exportSongImages(ids, outputId, file))
        }
    }

    function deleteText() {
        const n = Math.max(1, tab.selectedSongIds.length)
        if (n > 1)
            return qsTr("Permanently delete %1 songs from the library?").arg(n)
        return qsTr("Permanently delete this song from the library?")
    }

    function deleteSelected() {
        if (!tab.canDelete)
            return ""
        const ids = tab.selectedSongIds.slice()
        for (let i = 0; i < ids.length; ++i) {
            const err = tab.session.library.deleteSong(ids[i])
            if (err.length)
                return err
        }
        tab.songSelection.clear()
        return ""
    }

    function addToService() {
        if (!tab.canAdd)
            return
        const payload = tab.dragPayload()
        if (!tab.applyLibraryPayload(payload)) {
            const ids = payload.songIds || []
            for (let i = 0; i < ids.length; ++i)
                tab.session.library.addSongToService(ids[i])
        }
    }

    function syncSongTitle() {
        const current = tab.songSelection.current
        if (current === null) {
            tab.selectedSongTitle = ""
            return
        }
        const row = tab.songSelection.rowOfKey(current)
        if (row >= 0)
            tab.selectedSongTitle = String(MultiSelect.modelValue(songList.model, row,
                                                                  SongListModel.TitleRole) || "")
    }

    function dragPayload() {
        return { kind: "songs", songIds: tab.selectedSongIds.slice() }
    }

    function dragLabel() {
        if (tab.selectedSongIds.length <= 1)
            return tab.selectedSongTitle
        return qsTr("%1 songs").arg(tab.selectedSongIds.length)
    }

    ListView {
        id: songList
        anchors.fill: parent
        clip: true
        model: tab.session ? tab.session.library.songs : null
        spacing: tab.rowGap
        boundsBehavior: Flickable.StopAtBounds
        interactive: !tab.songSelection.marqueeActive && !tab.libDragging
        ScrollBar.vertical: AppScrollBar {}

        delegate: Rectangle {
            id: songDelegate
            required property var songId
            required property string title
            required property string authors
            /// The lyric line a search found the song by, if that was all.
            required property string matchedLine
            required property int index
            readonly property string subtitle: songDelegate.matchedLine.length
                                               ? "\u201c" + songDelegate.matchedLine + "\u201d"
                                               : songDelegate.authors
            readonly property bool chosen: tab.songSelection.contains(songDelegate.songId)
            width: ListView.view.width - Style.scrollGutter(ListView.view)
            height: songBody.implicitHeight + tab.rowPadV
            color: Style.panelRowBg(songDelegate.chosen, songHover.containsMouse)
            radius: Style.radiusSmall
            ListRowAccent {
                cornerRadius: parent.radius
                selected: songDelegate.chosen
                hovered: songHover.containsMouse
            }

            Column {
                id: songBody
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: tab.rowPadH
                anchors.rightMargin: tab.rowPadH
                spacing: 3

                Text {
                    width: parent.width
                    text: songDelegate.title
                    color: songDelegate.chosen ? Style.textStrong : Style.textBright
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: songDelegate.subtitle.length > 0
                    height: visible ? implicitHeight : 0
                    text: songDelegate.subtitle
                    color: songDelegate.chosen ? Style.textSoft : Style.textCaption
                    font.pixelSize: Style.fontSmall
                    font.italic: songDelegate.matchedLine.length > 0
                    elide: Text.ElideRight
                }
            }

            LibraryRowPress {
                id: songHover
                libraryTab: tab
                selection: tab.songSelection
                key: songDelegate.songId
                onDragStarted: function (x, y) {
                    tab.beginLibAdd(tab.dragPayload(), tab.dragLabel(), songHover, x, y)
                }
                onActivated: function (live) { tab.session.live.playSong(songDelegate.songId, live) }
            }
        }

        MarqueeRect {
            view: songList
            active: tab.songSelection.marqueeActive
            contentX1: tab.songSelection.marqueeX1
            contentY1: tab.songSelection.marqueeY1
            contentX2: tab.songSelection.marqueeX2
            contentY2: tab.songSelection.marqueeY2
        }

        Text {
            anchors.centerIn: parent
            visible: songList.count === 0
            z: 1
            text: tab.search.trim().length > 0
                  ? qsTr("Nothing matches your search.")
                  : qsTr("No songs")
            color: Style.textMuted
            font.pixelSize: Style.fontUi
        }
    }

    // Empty space draws a marquee.
    LibraryMarqueeArea {
        libraryTab: tab
        list: songList
        selection: tab.songSelection
    }

    /// The song editor, made the first time it is asked for.
    function songEditor(): SongEditDialog {
        return songEditLoader.load() as SongEditDialog
    }

    LazyLoader {
        id: songEditLoader
        objectName: "SongEditDialog"
        sourceComponent: SongEditDialog {
            session: tab.session
        }
    }

    FileDialog {
        id: songDialog
        title: qsTr("Import Songs")
        fileMode: FileDialog.OpenFiles
        // The file says what it is; these only narrow the list. OpenSong
        // files usually have no extension, so they show under All files.
        nameFilters: [
            qsTr("Song files (*.xml *.txt *.cho *.crd *.chopro *.chordpro *.chord *.pro *.usr *.bin)"),
            qsTr("ChordPro (*.cho *.crd *.chopro *.chordpro *.chord *.pro)"),
            qsTr("SongSelect (*.txt *.usr *.bin)"),
            qsTr("OpenLyrics, OpenSong (*.xml)"),
            qsTr("All files (*)")
        ]
        onAccepted: tab.importPicked(selectedFiles.map(u => u.toString()))
    }

    /// One file imports at once. Several open the window that asks which.
    function importPicked(paths) {
        if (paths.length > 1) {
            const chooser = songImportLoader.load() as SongImportDialog
            chooser.openFor(paths)
        } else
            tab.reportImport(tab.session.library.importSongs(paths))
    }

    /// Say what an import could not do: a file it could not read, and what it
    /// left out of a song it did. A clean import says nothing; the songs are
    /// in the list.
    function reportImport(result) {
        const said = (result.errors || []).concat(result.warnings || [])
        if (said.length === 0)
            return
        importReport.title = result.count === 0 ? qsTr("Nothing was imported")
                           : result.count === 1 ? qsTr("1 song imported")
                                                : qsTr("%1 songs imported").arg(result.count)
        importReport.ask(said.join("\n"))
    }

    LazyLoader {
        id: songImportLoader
        objectName: "SongImportDialog"
        sourceComponent: SongImportDialog {
            session: tab.session
            onImported: result => tab.reportImport(result)
        }
    }

    AppDialog {
        id: importReport
        objectName: "songImportReport"
        acknowledge: true
    }
}
