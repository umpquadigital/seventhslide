pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import SeventhSlide

// Song editor, in a WindowShell: Lyrics (title, order, verses), Details
// (authors and copyright) and Topics & Comments - those two for a library song
// only - and Themes (the theme changes in the order).
// SongEditState holds the song and every edit to it; this is the sheet
// around its panels, plus the pieces only a view has: the drag ghost, the
// theme picker and map editor, the custom-credit and arrangement name prompts,
// focus.
// A focus scope: the click-away area inside takes the keys only while the
// editor is open, never from the window behind it while it is closed.
FocusScope {
    id: root

    required property SessionFacade session
    /// Whether the editor is up.
    readonly property alias open: songEditState.open
    /// The tab showing: "lyrics", "authors", "topics" or "themes".
    property alias tab: songEditState.tab

    visible: open
    z: Style.sheetZ

    SongEditState {
        id: songEditState
        session: root.session
        themeMapEditor: mapEditor
        onCustomRoleAsked: authorCustomRoleDialog.ask("")
        onArrangementNameAsked: function (mode, initial) {
            arrangementNamePrompt.mode = mode
            arrangementNamePrompt.ask(initial)
        }
    }

    /// Edit the song of rundown item ``id``; changes stay in that service.
    function openItem(id) {
        songEditState.openItem(id)
    }

    /// Edit library song ``id``.
    function openLibrary(id) {
        songEditState.openLibrary(id)
    }

    /// Start a new library song.
    function openNewLibrary() {
        songEditState.openNewLibrary()
    }

    function closeThemePicker() {
        themePicker.close()
    }

    function clearFieldFocus() {
        lyricsPanel.clearFieldFocus()
    }

    function close() {
        root.clearFieldFocus()
        root.closeThemePicker()
        songEditState.close()
    }

    function save() {
        if (songEditState.save())
            root.close()
    }

    /// Close as the operator asks for it: unsaved changes are not lost unasked.
    function requestCancel() {
        root.clearFieldFocus()
        if (songEditState.hasChanges())
            discardConfirm.open = true
        else
            root.close()
    }

    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: {
            if (themePicker.open)
                root.closeThemePicker()
            else
                root.requestCancel()
        }
    }

    WindowShell {
        id: modalBox
        anchors.centerIn: parent
        width: Math.min(1000, parent.width - 40)
        height: Math.min(700, parent.height - 40)
        title: qsTr("Song")
        pages: songEditState.libraryMode
               ? [{ id: "lyrics", label: qsTr("Lyrics"), icon: "song" },
                  { id: "authors", label: qsTr("Details"), icon: "feather" },
                  { id: "topics", label: qsTr("Topics & Comments"), icon: "tag" },
                  { id: "themes", label: qsTr("Themes"), icon: "image" }]
               : [{ id: "lyrics", label: qsTr("Lyrics"), icon: "song" },
                  { id: "themes", label: qsTr("Themes"), icon: "image" }]
        page: songEditState.tab
        onPageActivated: function (id) { root.showTab(id) }
        note: songEditState.libraryMode ? "" : qsTr("Changes are saved to this service only")
        errorText: songEditState.errorText
        onCanceled: root.requestCancel()
        onAccepted: root.save()

        MouseArea {
            id: modalClickAway
            anchors.fill: parent
            focus: true
            onClicked: root.clearFieldFocus()

            SongLyricsPanel {
                id: lyricsPanel
                anchors.fill: parent
                visible: songEditState.tab === "lyrics"
                dialog: songEditState
                ghostLayer: root
                focusSink: modalClickAway
            }
            SongDetailsPanel {
                anchors.fill: parent
                visible: songEditState.tab === "authors" && songEditState.libraryMode
                dialog: songEditState
            }
            SongTopicsPanel {
                anchors.fill: parent
                visible: songEditState.tab === "topics" && songEditState.libraryMode
                dialog: songEditState
            }
            SongThemesPanel {
                anchors.fill: parent
                visible: songEditState.tab === "themes"
                dialog: songEditState
            }
        }
    }

    // Ghost chip while dragging a verse (reorder or play-order) or reordering
    // chips. Over the whole window; its position is in this item's
    // coordinates (the lyrics panel's ghostLayer).
    Rectangle {
        id: verseOrderGhost
        visible: songEditState.orderGhostActive
        z: 2
        width: ghostLabel.implicitWidth + 14
        height: 24
        // The ghost is a synthesized badge, not a copy of the grabbed row,
        // so it hangs from a fixed hotspot near its own left edge. Binding
        // (rather than assigning on each move) keeps it under the cursor
        // when the badge resizes to a different verse code mid-drag.
        x: lyricsPanel.orderGhostX - Math.min(12, width / 2)
        y: lyricsPanel.orderGhostY - height / 2
        radius: Style.radiusSmall
        color: songEditState.chipDragActive && songEditState.chipDragTheme ? Style.chipThemeBg
             : (songEditState.chipDragActive && songEditState.chipDragTitle ? Style.chipTitleBg : Style.chipVerseBg)
        border.color: Style.accent
        opacity: 0.95

        Text {
            id: ghostLabel
            anchors.centerIn: parent
            text: songEditState.chipDragActive
                  ? songEditState.chipLabel(songEditState.chipDragCode)
                  : songEditState.verseDragCode
            color: songEditState.chipDragActive && songEditState.chipDragTheme ? Style.chipThemeInk
                 : (songEditState.chipDragActive && songEditState.chipDragTitle ? Style.chipTitleInk : Style.chipVerseInk)
            font.pixelSize: Style.fontSmall
            font.bold: true
        }
    }

    ThemePickerDialog {
        id: themePicker
        session: root.session
    }

    // A slot edits the map of the selected theme chip, which is written back
    // to the chip after each change.
    ThemeMapEditor {
        id: mapEditor
        session: root.session
        picker: themePicker
        scope: "song"
        inheritShort: songEditState.themeInheritShort
        editable: songEditState.selectedThemeCode.length > 0
        onEdited: songEditState.flushSelectedChipMap()
    }

    DiscardConfirm {
        id: discardConfirm
        objectName: "songDiscardConfirm"
        onOpenChanged: if (!open && root.open) root.forceActiveFocus()
        onAccepted: root.close()
    }

    AppPromptDialog {
        id: authorCustomRoleDialog
        title: qsTr("Other credit")
        label: qsTr("How should this credit read?")
        placeholder: qsTr("Paraphrase")
        acceptLabel: qsTr("Add")
        onAccepted: songEditState.addSelectedAuthorCustomRole(authorCustomRoleDialog.value)
    }

    AppPromptDialog {
        id: arrangementNamePrompt
        property string mode: "new"
        title: arrangementNamePrompt.mode === "new" ? qsTr("New arrangement")
                                                     : qsTr("Rename arrangement")
        placeholder: qsTr("Arrangement name")
        acceptLabel: arrangementNamePrompt.mode === "new" ? qsTr("Create") : qsTr("Rename")
        onAccepted: songEditState.submitArrangementName(arrangementNamePrompt.mode,
                                                        arrangementNamePrompt.value)
    }

    Keys.onEscapePressed: function (event) {
        if (themePicker.open) {
            root.closeThemePicker()
            event.accepted = true
            return
        }
        if (songEditState.verseDragActive) {
            songEditState.cancelVerseOrderDrag()
            event.accepted = true
            return
        }
        if (songEditState.chipDragActive) {
            songEditState.cancelChipOrderDrag()
            event.accepted = true
            return
        }
        root.requestCancel()
    }
    focus: open

    /// Switch to page ``id``, letting go of whatever the page being left had
    /// in hand: a field's focus, a drag, an open menu.
    function showTab(id) {
        root.clearFieldFocus()
        songEditState.cancelVerseOrderDrag()
        songEditState.cancelChipOrderDrag()
        songEditState.closeAuthor()
        songEditState.tab = id
        if (id === "themes")
            songEditState.ensureThemeChip(true)
    }
}
