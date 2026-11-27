pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// Libraries & Profiles page of Settings: one well split into a column of
// libraries and a column of the open library's profiles, and beside it one
// column of buttons for whichever column was used last. Its state and actions
// live on SettingsLibraryState; the prompts, confirms and folder chooser are
// the dialog's.
Item {
    id: panel

    required property SettingsLibraryState dialog
    /// ``dialog.session``, typed, so calls on it are checked.
    readonly property SessionFacade session: panel.dialog.session
    readonly property bool onLibraries: panel.dialog.activeColumn === "library"
    readonly property var library: panel.dialog.selectedLibrary

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Libraries & Profiles")
            text: qsTr("A library is where your songs, services, pictures and themes are saved. "
                       + "Profiles let one library keep more than one setup, so a main event "
                       + "and a youth night can each have their own look.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            ListWell {
                Layout.fillWidth: true
                Layout.fillHeight: true

                BrowserColumn {
                    id: librariesColumn
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    title: qsTr("Libraries")
                    iconSource: Icons.url("database", librariesColumn.titleInk, 2)
                    focused: panel.onLibraries
                    model: panel.dialog.libraryRows
                    delegate: PickRow {
                        required property var modelData
                        readonly property string rowId: String(modelData.id)
                        readonly property string cloud: String(modelData.cloudService || "")
                        /// How it syncs, when it does (SyncController).
                        readonly property var sync: panel.dialog.syncStates[rowId] || null
                        readonly property bool syncTrouble: !!sync && !!sync.trouble
                        title: String(modelData.name)
                        iconSource: sync ? Icons.url("sync", sync.state === "uptodate" ? Style.runningDot
                                                     : syncTrouble ? Style.textWarning : Style.textCaption, 2)
                                         : ""
                        subtitle: modelData.missing ? qsTr("Folder not found")
                                : cloud.length ? qsTr("Synced by %1, which can damage it").arg(cloud)
                                : sync ? (sync.viewOnly ? qsTr("View only · %1").arg(sync.text) : sync.text)
                                : String(modelData.path)
                        subtitleInk: modelData.missing || cloud.length || syncTrouble ? Style.textWarning
                                   : (selected ? Style.textSoft : Style.textCaption)
                        dimmed: !!modelData.missing
                        checked: !!modelData.active
                        trailingArrow: true
                        selected: panel.onLibraries && panel.dialog.selectedLibraryId === rowId
                        trail: !panel.onLibraries && panel.dialog.selectedLibraryId === rowId
                        onClicked: panel.dialog.chooseLibrary(rowId)
                        onDoubleClicked: panel.dialog.openSelectedLibrary()
                    }
                }
                BrowserColumn {
                    id: profilesColumn
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                    ruleLeft: true
                    title: qsTr("Profiles")
                    iconSource: Icons.url("sliders", profilesColumn.titleInk, 2)
                    focused: !panel.onLibraries
                    model: panel.dialog.showsProfiles ? panel.dialog.profileRows : []
                    emptyText: !panel.library ? ""
                               : panel.library.missing ? qsTr("This library’s folder is missing.")
                               : panel.dialog.showsProfiles ? qsTr("No profiles yet.")
                               : qsTr("Open this library to see its profiles.")
                    delegate: PickRow {
                        required property var modelData
                        title: String(modelData.name)
                        checked: !!modelData.active
                        selected: !panel.onLibraries && panel.dialog.selectedProfileId === modelData.id
                        onClicked: panel.dialog.chooseProfile(modelData.id)
                        onDoubleClicked: panel.dialog.useSelectedProfile()
                    }
                }
            }

            // One set of buttons, for the column used last. Not filling: its
            // buttons fill it, which a layout would otherwise pass up.
            ColumnLayout {
                Layout.fillWidth: false
                Layout.preferredWidth: 112
                Layout.maximumWidth: 112
                Layout.fillHeight: true
                spacing: 6

                ColumnButton {
                    visible: panel.onLibraries
                    primary: true
                    label: qsTr("Open")
                    enabled: panel.dialog.canOpenLibrary()
                    onClicked: panel.dialog.openSelectedLibrary()
                }
                ColumnButton {
                    id: newLibraryButton
                    visible: panel.onLibraries
                    label: qsTr("New…")
                    onClicked: newLibraryMenu.visible ? newLibraryMenu.close() : newLibraryMenu.openAt()

                    AppMenu {
                        id: newLibraryMenu
                        anchorItem: newLibraryButton
                        MenuItemRow {
                            icon: "plus"
                            label: qsTr("Create New Library…")
                            onTriggered: {
                                newLibraryMenu.close()
                                panel.dialog.askNewLibrary()
                            }
                        }
                        MenuItemRow {
                            icon: "folderPlus"
                            label: qsTr("Add Existing Folder…")
                            onTriggered: {
                                newLibraryMenu.close()
                                panel.dialog.openLibraryFolder()
                            }
                        }
                        MenuItemRow {
                            visible: panel.dialog.canJoin
                            icon: "sync"
                            label: qsTr("Join a Synced Library…")
                            onTriggered: {
                                newLibraryMenu.close()
                                panel.dialog.askJoinLibrary()
                            }
                        }
                    }
                }
                ColumnButton {
                    visible: panel.onLibraries
                    label: qsTr("Rename…")
                    enabled: !!panel.library
                    onClicked: panel.dialog.askRenameLibrary()
                }
                ColumnButton {
                    visible: panel.onLibraries
                    label: Platform.showFolderText
                    enabled: !!panel.library && !panel.library.missing
                    onClicked: panel.dialog.showLibraryFolder()
                }

                ColumnButton {
                    visible: !panel.onLibraries
                    primary: true
                    label: qsTr("Use")
                    enabled: panel.dialog.canUseProfile()
                    onClicked: panel.dialog.useSelectedProfile()
                }
                ColumnButton {
                    visible: !panel.onLibraries
                    label: qsTr("New…")
                    enabled: !!panel.session && panel.session.ready
                    onClicked: panel.dialog.askNewProfile()
                }
                ColumnButton {
                    visible: !panel.onLibraries
                    label: qsTr("Duplicate")
                    enabled: panel.dialog.selectedProfileId >= 0
                    onClicked: panel.dialog.duplicateSelectedProfile()
                }
                ColumnButton {
                    visible: !panel.onLibraries
                    label: qsTr("Rename…")
                    enabled: panel.dialog.selectedProfileId >= 0
                    onClicked: panel.dialog.askRenameProfile()
                }

                Item { Layout.fillHeight: true }

                ColumnButton {
                    visible: panel.onLibraries
                    danger: true
                    label: qsTr("Remove…")
                    enabled: !!panel.library && !panel.library.active
                    onClicked: panel.dialog.beginRemoveLibrary()
                }
                ColumnButton {
                    visible: !panel.onLibraries
                    danger: true
                    label: qsTr("Delete…")
                    enabled: panel.dialog.selectedProfileId >= 0 && panel.dialog.profileRows.length > 1
                    onClicked: panel.dialog.beginDeleteProfile()
                }
            }
        }
    }

    component ColumnButton: AppButton {
        Layout.fillWidth: true
        size: "control"
    }

    /// One column of the well: a header, then its rows.
    /// A column of the browser: its title, and its rows in a list.
    component BrowserColumn: WellPane {
        id: column
        property bool focused: false
        property var model: []
        property Component delegate: null
        property string emptyText: ""

        titleInk: column.focused ? Style.textSoft : Style.textCaption

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The bar a gap in from the well's edge, as the Library's is.
            Layout.rightMargin: Style.scrollGap
            topMargin: 6
            bottomMargin: 6
            leftMargin: 6
            rightMargin: Style.scrollGutter(list)
            spacing: 3
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: column.model
            delegate: column.delegate
            ScrollBar.vertical: AppScrollBar {}

            Text {
                visible: list.count === 0 && column.emptyText.length > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: column.emptyText
                color: Style.textCaption
                font.pixelSize: Style.fontBody
            }
        }
    }
}
