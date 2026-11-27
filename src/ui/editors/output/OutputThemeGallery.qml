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
            return qsTr("Backgrounds sit behind the words on this screen, and can be a color, a "
                        + "picture or a video. Mark one as the Song or Bible default to use it "
                        + "every time.")
        case "bible":
            return qsTr("Bible themes decide how verses and their references look on this screen. "
                        + "The one marked Default is used unless a service or item picks another.")
        case "media":
            return qsTr("Media themes decide how pictures, videos and web pages fill this screen, "
                        + "and how their audio fades in and out.")
        default:
            return qsTr("Song themes decide how lyrics look on this screen, from the font and "
                        + "color to where the words sit. The one marked Default is used unless a "
                        + "service or song picks another.")
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
