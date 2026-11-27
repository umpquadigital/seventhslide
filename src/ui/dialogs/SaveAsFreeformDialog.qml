import SeventhSlide

// Save as Freeform: snapshot a library song or a Bible passage.
AppPromptDialog {
    id: root

    required property SessionFacade session
    /// Library song to snapshot; negative unless a song conversion is armed.
    property int songId: -1
    /// Browse-selection verse range; both zero unless a Bible conversion is armed.
    property int bibleVerseStart: 0
    property int bibleVerseEnd: 0

    /// The save failed; ``message`` says why. This prompt has closed by then,
    /// so the owner shows it.
    signal failed(string message)

    title: qsTr("Save as Freeform")
    label: qsTr("Name")
    acceptLabel: qsTr("Create")

    function prepareSong(id) {
        root.songId = id
        root.bibleVerseStart = 0
        root.bibleVerseEnd = 0
    }

    function prepareBible(verseStart, verseEnd) {
        root.songId = -1
        root.bibleVerseStart = verseStart
        root.bibleVerseEnd = verseEnd
    }

    onAccepted: {
        if (!root.session)
            return
        let result
        if (root.songId >= 0)
            result = root.session.saveSongAsFreeform(root.songId, root.value)
        else if (root.bibleVerseStart > 0)
            result = root.session.saveBibleAsFreeform(root.bibleVerseStart,
                                                      root.bibleVerseEnd, root.value)
        else
            return
        if (result && result.error && result.error.length)
            root.failed(result.error)
    }
}
