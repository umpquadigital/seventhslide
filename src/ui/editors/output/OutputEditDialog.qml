pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import SeventhSlide

// Output editor: its pages in a WindowShell. A theme opened from its gallery
// is edited in the slide editor (SlideEditorWindow), which draws over this.
// OutputEditState is its view-model; this is the sheet around its panels,
// plus what only a view has: the confirms the state asks for, focus and keys.
// A focus scope: the click-away area inside takes the keys only while the
// editor is open, never from the window behind it while it is closed.
FocusScope {
    id: root

    required property SessionFacade session
    /// Whether the editor is up.
    readonly property alias open: outputEditState.open

    visible: open
    z: Style.sheetZ
    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    OutputEditState {
        id: outputEditState
        session: root.session
        onThemeDeleteAsked: function (message) { themeDeleteConfirm.ask(message) }
    }


    /// Open on output ``name``, at tab ``startTab`` ("general" by default).
    function openFor(name, startTab) {
        outputEditState.openFor(name, startTab)
    }

    function clearFieldFocus() {
        modalClickAway.forceActiveFocus()
    }


    AppDialog {
        id: themeDeleteConfirm
        title: qsTr("Delete theme")
        acceptLabel: qsTr("Delete")
        onAccepted: outputEditState.confirmDeleteTheme()
    }

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: outputEditState.close()
    }

    WindowShell {
        id: modalBox
        anchors.centerIn: parent
        width: Math.min(Style.outputWindowW, parent.width - 40)
        height: Math.min(Style.outputWindowH, parent.height - 40)
        color: Style.panelBg
        title: qsTr("Output")
        // General, a gallery per theme library, then Destinations. A text
        // theme's library has its content's icon (ContentTypes); Media's is
        // the video one, Backgrounds keeping the picture.
        readonly property var kindIcons: ({ song: "song", bible: "bible", media: "video", bg: "image" })
        pages: [{ id: "general", label: qsTr("General"), icon: "settings" }]
               .concat(outputEditState.themeKinds.map(function (k) {
                   return { id: k.kind, label: k.label, icon: modalBox.kindIcons[k.kind] }
               }))
               .concat([{ id: "destinations", label: qsTr("Destinations"), icon: "display" }])
        page: outputEditState.tab
        onPageActivated: function (id) {
            root.clearFieldFocus()
            outputEditState.switchTab(id)
        }
        closeOnly: outputEditState.tab !== "general"
        errorText: outputEditState.errorText
        onCanceled: outputEditState.close()
        onAccepted: outputEditState.saveGeneral()

        MouseArea {
            id: modalClickAway
            anchors.fill: parent
            focus: true
            onClicked: {
                outputEditState.closeGalleryMenus()
                root.clearFieldFocus()
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                // Body: the window's page area; a ScrollPage runs to the
                // window's edges.
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // ========== BROWSE: General ==========
                    OutputGeneralPanel {
                        id: outGeneralFlick
                        anchors.fill: parent
                        visible: outputEditState.tab === "general"
                        dialog: outputEditState
                    }

                    // ========== BROWSE: Destinations ==========
                    OutputDestinationsPanel {
                        anchors.fill: parent
                        visible: outputEditState.tab === "destinations"
                        dialog: outputEditState.destState
                    }

                    // ========== BROWSE: Theme gallery (each theme library) ==========
                    OutputThemeGallery {
                        id: themeGallery
                        anchors.fill: parent
                        visible: outputEditState.onThemeTab
                        dialog: outputEditState
                        focusSink: modalClickAway
                    }

                } // Body
            }
        }
    }

    Keys.onEscapePressed: {
        if (themeGallery.anyMenuOpen) {
            outputEditState.closeGalleryMenus()
            return
        }
        outputEditState.close()
    }
    Keys.onPressed: function (event) {
        if (outputEditState.onThemeTab
                && outputEditState.selectedId
                && !themeGallery.searchFocused
                && (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)) {
            outputEditState.deleteSelected()
            event.accepted = true
        }
    }
    focus: open
}
