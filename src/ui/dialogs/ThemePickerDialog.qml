import QtQuick
import SeventhSlide

// Picks a theme of one kind (song, Bible, media or background) for one output
// (song editor, service settings, Freeform fill), in a WindowShell: the theme gallery the output
// dialog shows, led by an Inherit tile, with Cancel / Select. The caller's
// hint (``subtitleText``) sits in the footer.
Item {
    id: root
    visible: open
    z: Style.pickerZ
    focus: open

    required property SessionFacade session

    property bool open: false
    property string outputName: ""
    property string kind: "song" // one of session.themes.kinds
    property string selectedId: ""
    property bool showInherit: true
    property string inheritLabel: qsTr("Inherit")
    property string inheritCaption: qsTr("Inherit")
    property string titleText: ""
    property string subtitleText: ""
    property int galleryRevision: 0
    property var defaults: ({})

    signal accepted(string themeId)
    signal cancelled()

    function openFor(output, themeKind, currentId) {
        root.outputName = String(output || "")
        root.kind = String(themeKind || "")
        root.selectedId = String(currentId || "")
        root.resetBrowse()
        root.defaults = (root.session && root.outputName.length)
                        ? (root.session.themes.categoryDefaults(root.outputName) || {})
                        : ({})
        root.galleryRevision += 1
        root.open = true
    }

    function close() {
        root.open = false
        root.resetBrowse()
    }

    function resetBrowse() {
        gallery.reset()
        gallery.defaultsOnly = false
    }

    function confirm() {
        root.accepted(root.selectedId)
        root.close()
    }

    // ``themes.list`` is a method, so a binding that called it directly would
    // never re-run; naming the revisions here makes it track them.
    readonly property var themes: (
        (root.session ? root.session.themes.libraryRevision : 0),
        root.galleryRevision,
        ((root.session && root.outputName.length)
         ? (root.session.themes.list(root.outputName, root.kind) || [])
         : [])
    )

    readonly property string resolvedTitle: {
        if (root.titleText.length)
            return root.titleText
        switch (root.kind) {
        case "bg":
            return qsTr("Select Background Theme")
        case "bible":
            return qsTr("Select Bible Theme")
        case "media":
            return qsTr("Select Media Theme")
        default:
            return qsTr("Select Song Theme")
        }
    }
    readonly property string resolvedSubtitle: {
        if (root.subtitleText.length)
            return root.subtitleText
        return root.kind === "bg"
            ? qsTr("Choose a background theme to apply on this output, or inherit from above.")
            : qsTr("Choose a theme to apply on this output, or inherit from above.")
    }
    anchors.fill: parent

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: {
            root.cancelled()
            root.close()
        }
    }

    WindowShell {
        id: box
        anchors.centerIn: parent
        width: Math.min(820, parent.width - 40)
        height: Math.min(640, parent.height - 40)
        title: root.resolvedTitle
        note: root.resolvedSubtitle
        acceptLabel: qsTr("Select")
        onCanceled: {
            root.cancelled()
            root.close()
        }
        onAccepted: root.confirm()

        // A click on the page's own surface lets go of the search and menus.
        MouseArea {
            anchors.fill: parent
            onClicked: {
                gallery.releaseSearch()
                gallery.closeMenus()
            }
        }

        ThemeGalleryView {
            id: gallery
            anchors.fill: parent
            session: root.session
            themes: root.themes
            defaults: root.defaults
            outputName: root.outputName
            kind: root.kind
            selectedId: root.selectedId
            showInherit: root.showInherit
            inheritLabel: root.inheritLabel
            inheritCaption: root.inheritCaption
            onPicked: function (themeId) { root.selectedId = themeId }
            onActivated: function (themeId) {
                root.selectedId = themeId
                root.confirm()
            }
            onSearchEscaped: {
                root.cancelled()
                root.close()
            }
        }
    }

    Keys.onEscapePressed: function (event) {
        if (!root.open)
            return
        if (gallery.menuOpen) {
            gallery.closeMenus()
            event.accepted = true
            return
        }
        root.cancelled()
        root.close()
        event.accepted = true
    }
}
