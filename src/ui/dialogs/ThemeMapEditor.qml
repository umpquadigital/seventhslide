import QtQuick
import SeventhSlide
import "../js/ThemeMap.js" as ThemeMap

// Edits one theme map (for each output, a theme and a background per category)
// on the Themes page of the song editor or the service settings: what the
// ThemeSlots show, and the round trip through the theme picker that changes
// a slot. ThemeMap.js holds the map rules; which slots there are is the
// C++'s (ThemeController.pickSlots), for the map's ``scope``.
//
// The map is keyed by output id; theme libraries and thumbnails are looked up
// by output name.
QtObject {
    id: editor

    required property SessionFacade session
    /// The picker a slot opens.
    required property ThemePickerDialog picker
    /// Whose map it is: "service", or a content kind ("song" for a song's
    /// theme chips). It decides the slots.
    required property string scope
    /// The map being edited. The owner loads it and reads it back to save.
    property var themeMap: ({})
    /// What an unset slot falls back to ("Use Service", "Output Default").
    property string inheritShort: ""
    /// Whether a slot may change the map right now (the song editor edits the
    /// map of the selected theme chip, so not while none is selected).
    property bool editable: true

    /// The slots each output shows, grouped by category:
    /// [{category, label, slots: [{category, field, kind, label}]}].
    readonly property var slotGroups: editor.session
                                      ? editor.session.themes.pickSlots(editor.scope) : []

    /// A slot changed the map.
    signal edited()

    property string _pickerOutputId: ""
    property var _pickerSlot: null

    function themeId(outputId, category, field) {
        return ThemeMap.themeId(editor.themeMap, outputId, category, field)
    }

    function setThemeId(outputId, category, field, themeId) {
        if (!editor.editable)
            return
        editor.themeMap = ThemeMap.withThemeId(editor.themeMap, outputId, category, field,
                                               themeId)
        editor.edited()
    }

    /// The name of theme ``themeId`` in the ``kind`` library ("Inherit" for
    /// none); "Unknown" when it no longer exists.
    function displayName(outputName, kind, themeId) {
        const id = String(themeId || "")
        if (!id.length)
            return qsTr("Inherit")
        if (!editor.session)
            return qsTr("Unknown")
        // libraryRevision is read as a value, so renames and replacements
        // refresh the name.
        if (editor.session.themes.libraryRevision < 0)
            return qsTr("Unknown")
        const themes = editor.session.themes.list(outputName, kind) || []
        for (let i = 0; i < themes.length; ++i) {
            if (String(themes[i].id || "") === id)
                return String(themes[i].name || id)
        }
        return qsTr("Unknown")
    }

    function thumbUrl(outputName, kind, themeId) {
        if (!editor.session)
            return ""
        return ThemeMap.thumbUrl(outputName, kind, themeId,
                                 editor.session.themes.libraryRevision)
    }

    /// Open the picker for one of ``outputId``'s slots (a ``slotGroups`` slot).
    function openPicker(outputId, outputName, slot) {
        editor._pickerOutputId = outputId
        editor._pickerSlot = slot
        editor.picker.titleText = ""
        editor.picker.showInherit = true
        // The Inherit card is named as the slots name it; its thumbnail is
        // the same empty frame.
        editor.picker.inheritLabel = ""
        editor.picker.inheritCaption = editor.inheritShort
        editor.picker.openFor(outputName, slot.kind,
                              editor.themeId(outputId, slot.category, slot.field))
    }

    property Connections _pickerAnswer: Connections {
        target: editor.picker
        function onAccepted(themeId) {
            const slot = editor._pickerSlot
            if (slot)
                editor.setThemeId(editor._pickerOutputId, slot.category, slot.field, themeId)
        }
    }
}
