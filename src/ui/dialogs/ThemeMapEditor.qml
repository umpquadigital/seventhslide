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
    /// What a slot can take in place of a theme of its own, first the one an
    /// unset slot means: each ``{id, slotName, caption, label, previous}``, with
    /// the id stored for it, what a slot shows ("Previous", "Output Default"),
    /// the picker tile's name and the words in its frame, and whether it takes
    /// ``previousMap``'s theme.
    property var inheritChoices: [{ id: "", slotName: "", caption: "", label: "", previous: false }]
    /// What a slot that takes the previous theme takes, a map of this shape
    /// (the song editor's change before the selected one).
    property var previousMap: ({})
    /// The picker's subtitle per theme library; empty keeps the picker's own
    /// wording.
    property string pickerTextSubtitle: ""
    property string pickerBgSubtitle: ""
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

    /// The choice of ``inheritChoices`` that ``themeId`` stands for, or null
    /// for a theme.
    function inheritChoice(themeId) {
        const id = String(themeId || "")
        for (let i = 0; i < editor.inheritChoices.length; ++i) {
            if (String(editor.inheritChoices[i].id || "") === id)
                return editor.inheritChoices[i]
        }
        return id.length ? null : editor.inheritChoices[0] || null
    }

    /// The theme ``outputId``'s slot takes through its inherit choice
    /// ``themeId``, where the editor knows it; "" otherwise.
    function inheritedId(outputId, category, field, themeId) {
        const choice = editor.inheritChoice(themeId)
        return choice && choice.previous
               ? ThemeMap.themeId(editor.previousMap, outputId, category, field) : ""
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
        editor.picker.subtitleText = slot.kind === "bg" ? editor.pickerBgSubtitle
                                                        : editor.pickerTextSubtitle
        editor.picker.inheritTiles = editor.inheritChoices.map(function (choice) {
            return { id: choice.id, label: choice.label, caption: choice.caption }
        })
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
