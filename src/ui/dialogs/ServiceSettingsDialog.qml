pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import SeventhSlide

// A service's settings, in a WindowShell: its title, date, time and tags
// (General), its theme on each screen (Themes), and its slides saved as
// images (Export). The service toolbar opens it on the open service, the
// Services window on the one picked there. Nothing is kept until Save; an
// export runs when it is asked for.
Item {
    id: root
    visible: open
    // Over the Services window, which opens it on the service picked there.
    z: Style.sheetZ + 1
    focus: open

    required property SessionFacade session

    property bool open: false
    property string errorText: ""
    property string tab: "general" // general | themes | export
    property var serviceId: -1
    property alias themeMap: themeMapEditor.themeMap
    /// The theme map as it was opened, to tell whether it changed.
    property string loadedThemeMap: ""

    /// The window closed, saved or not.
    signal closed()

    onTabChanged: {
        if (themePicker.open)
            themePicker.close()
    }

    /// The open service's settings, on General (the service toolbar's gear),
    /// or on page ``page`` ("export": the toolbar's Export service…).
    function openDialog(page) {
        if (!root.session)
            return
        root.openFor(root.session.openServiceId, false)
        if (page && root.open)
            root.tab = page
    }

    /// Service ``which``'s settings, on General; ``focusTitle`` puts the
    /// cursor in its title (a service just made, to be named).
    function openFor(which, focusTitle) {
        if (!root.session || which < 0)
            return
        const details = root.session.library.serviceDetails(which)
        if (details.name === undefined)
            return
        root.serviceId = which
        general.load(details)
        exportState.load(which)
        root.themeMap = root.session.library.getServiceThemeMap(which)
        root.loadedThemeMap = JSON.stringify(root.themeMap)
        root.errorText = ""
        root.tab = "general"
        root.open = true
        root.forceActiveFocus()
        if (focusTitle)
            Qt.callLater(generalPage.focusTitle)
    }

    function close() {
        if (themePicker.open)
            themePicker.close()
        const was = root.open
        root.open = false
        root.errorText = ""
        root.themeMap = {}
        root.serviceId = -1
        if (was)
            root.closed()
    }

    /// Whether closing now would lose anything.
    function hasChanges() {
        return root.open && (general.hasChanges()
                             || JSON.stringify(root.themeMap) !== root.loadedThemeMap)
    }

    /// Close as the operator asks for it: unsaved changes are not lost unasked.
    function requestCancel() {
        if (themePicker.open)
            themePicker.close()
        if (root.hasChanges())
            discardConfirm.open = true
        else
            root.close()
    }

    function save() {
        if (!root.session || root.serviceId < 0)
            return
        const library = root.session.library
        let err = general.save(library, root.serviceId)
        if (err.length)
            root.tab = "general"
        else
            err = library.setServiceThemeMap(root.serviceId, root.themeMap)
        if (err.length) {
            root.errorText = err
            return
        }
        root.close()
    }

    /// General's drafts: the title, date, time and tags, kept on Save.
    ServiceGeneralState {
        id: general
    }

    /// Export's choice and how its export went; it exports with the themes
    /// this window holds.
    ServiceExportState {
        id: exportState
        session: root.session
        themeMap: root.themeMap
        serviceName: general.draftName.trim() || general.saved.name || ""
    }

    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: {
            if (themePicker.open)
                themePicker.close()
            else
                root.requestCancel()
        }
    }

    WindowShell {
        id: modalBox
        anchors.centerIn: parent
        width: Math.min(Style.settingsWindowW, parent.width - 40)
        height: Math.min(Style.settingsWindowH, parent.height - 40)
        title: qsTr("Service Settings")
        pages: [
            { id: "general", label: qsTr("General"), icon: "settings" },
            { id: "themes", label: qsTr("Themes"), icon: "image" },
            { id: "export", label: qsTr("Export"), icon: "export" }
        ]
        page: root.tab
        onPageActivated: function (id) { root.tab = id }
        errorText: root.errorText
        onCanceled: root.requestCancel()
        onAccepted: root.save()

        ServiceGeneralPage {
            id: generalPage
            anchors.fill: parent
            visible: root.tab === "general"
            dialog: general
        }

        ScrollPage {
            id: themesPage
            anchors.fill: parent
            visible: root.tab === "themes"

            PageHeader {
                width: themesPage.columnWidth
                title: qsTr("Themes")
                text: qsTr("The look of this service on each screen, for its songs, its Bible "
                           + "passages and its pictures, videos and web pages. Leave one on "
                           + "Output Default to keep its usual look.")
            }

            Text {
                visible: themeOutRepeater.count === 0
                width: themesPage.columnWidth
                topPadding: 14
                text: qsTr("No outputs yet.")
                color: Style.textCaption
                font.pixelSize: Style.fontBody
            }

            // One block per screen: its name, then a text theme and a
            // background for songs and for Bible, and a text theme for media.
            Repeater {
                id: themeOutRepeater
                model: root.session ? root.session.outputs : null
                delegate: Item {
                    id: screen
                    required property string outputId
                    required property string name
                    width: themesPage.columnWidth
                    height: block.height + 14

                    ScreenThemeSlots {
                        id: block
                        y: 14
                        width: parent.width
                        map: themeMapEditor
                        outputId: screen.outputId
                        outputName: screen.name
                    }
                }
            }
        }

        ServiceExportPage {
            anchors.fill: parent
            visible: root.tab === "export"
            dialog: exportState
        }
    }

    ThemePickerDialog {
        id: themePicker
        session: root.session
    }

    ThemeMapEditor {
        id: themeMapEditor
        session: root.session
        picker: themePicker
        scope: "service"
        inheritShort: qsTr("Output Default")
        pickerTextSubtitle: qsTr("Choose a theme for this output, or keep the output default.")
        pickerBgSubtitle: qsTr("Choose a background theme for this output, or keep the output default.")
    }

    Keys.onEscapePressed: function (event) {
        if (themePicker.open) {
            themePicker.close()
            event.accepted = true
            return
        }
        root.requestCancel()
        event.accepted = true
    }

    DiscardConfirm {
        id: discardConfirm
        objectName: "serviceDiscardConfirm"
        onOpenChanged: if (!open && root.open) root.forceActiveFocus()
        blurb: qsTr("Closing the window now loses what you have not saved.")
        onAccepted: root.close()
    }
}
