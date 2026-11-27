pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// The Output window's theme gallery pages (one per theme library), with
// each card's default chips, and on the well's bar Edit Design,
// Duplicate and Delete for the selected theme, and New.
// OutputEditState owns the themes and the selection, which the window's menus
// and Delete key act on too; the gallery owns how they are browsed, and resets
// and closes its menus when the state asks.
ThemeGalleryView {
    id: gallery

    /// The output editor's state. Every `gallery.dialog.` below is this
    /// panel's interface to it.
    required property OutputEditState dialog
    /// What takes focus when the search field lets go of it.
    required property Item focusSink

    readonly property bool anyMenuOpen: gallery.menuOpen

    session: gallery.dialog.session
    themes: gallery.dialog.themes
    defaults: gallery.dialog.defaults
    outputName: gallery.dialog.outputName
    kind: gallery.dialog.kind
    selectedId: gallery.dialog.selectedId
    editable: true
    title: {
        for (const k of gallery.dialog.themeKinds) {
            if (k.kind === gallery.dialog.kind)
                return k.label
        }
        return ""
    }
    hint: {
        switch (gallery.dialog.kind) {
        case "bg":
            return qsTr("What sits behind the words on this screen: a color, a picture or a "
                        + "video. Point at one and click Song or Bible to make it the default "
                        + "for songs or Bible verses. Pictures, videos and web pages fill the "
                        + "screen over black.")
        case "bible":
            return qsTr("How Bible verses look on this screen: the passage, its reference and "
                        + "where they sit. Point at one and click Default to use it for verses "
                        + "unless a service or the item picks another.")
        case "media":
            return qsTr("Where pictures, videos, web pages and live video sit on this screen, "
                        + "what goes over them, and how audio fades. Point at one and click "
                        + "Default to use it unless a service or the item picks another.")
        default:
            return qsTr("How song lyrics and title slides look on this screen: the font, size, "
                        + "color and where they sit. Point at one and click Default to use it "
                        + "for songs unless a service or the song picks another.")
        }
    }

    onPicked: function (themeId) { gallery.dialog.selectedId = themeId }
    onActivated: function (themeId) { gallery.dialog.enterDesign(themeId) }
    onDefaultToggled: function (category, field, themeId) {
        gallery.dialog.selectedId = themeId
        gallery.dialog.setDefault(category, field)
    }
    onSearchEscaped: gallery.focusSink.forceActiveFocus()

    Connections {
        target: gallery.dialog
        function onGalleryResetRequested() {
            gallery.reset()
        }
        function onGalleryMenusCloseRequested() {
            gallery.closeMenus()
        }
    }

    AppButton {
        objectName: "themeEditDesign"
        size: "control"
        iconSource: Icons.url("edit", ink)
        enabled: !!gallery.dialog.selectedId
        onClicked: gallery.dialog.enterDesign()
    }
    AppButton {
        objectName: "themeDuplicate"
        size: "control"
        iconSource: Icons.url("copy", ink)
        enabled: !!gallery.dialog.selectedId
        onClicked: gallery.dialog.duplicateSelected()
    }
    AppButton {
        objectName: "themeDelete"
        size: "control"
        iconSource: Icons.url("trash", ink)
        enabled: !!gallery.dialog.selectedId
        onClicked: gallery.dialog.deleteSelected()
    }
    AppButton {
        size: "control"
        primary: true
        label: qsTr("New")
        iconSource: Icons.url("plus", ink)
        onClicked: gallery.dialog.createNew()
    }
}
