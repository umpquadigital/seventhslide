pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Sync page of Settings: this computer (its name, the pause during
// services), its sync network (its port, its routable address, the upload
// speed cap) and the open library (synced or not, its computers, its admin
// work). Joining a library by an invite is on Libraries & Profiles. Its
// state and actions live on SettingsSyncState; the prompts and the codes it
// shows are the dialog's.
ScrollPage {
    id: panel

    required property SettingsSyncState dialog
    readonly property var library: panel.dialog.library
    /// The computers table's Role and Status columns.
    readonly property int roleW: 100
    readonly property int statusW: 230

    /// A computer's state, in words.
    function computerStatus(c) {
        if (c.invite)
            return qsTr("Expires %1").arg(panel.dialog.dateText(c.expires))
        if (c.thisComputer)
            return qsTr("This computer")
        if (c.removed)
            return qsTr("Removed")
        if (c.connected)
            return qsTr("Connected")
        if (c.lastHeard)
            return qsTr("Last heard %1").arg(new Date(c.lastHeard).toLocaleString(Qt.locale(), Locale.ShortFormat))
        return qsTr("Not heard from here")
    }

    PageHeader {
        width: panel.columnWidth
        title: qsTr("Sync")
        text: qsTr("Sync keeps a library the same on every computer that uses it. Changes made on "
                   + "one computer show up on the others.")
    }

    Text {
        visible: !panel.dialog.available
        width: panel.columnWidth
        wrapMode: Text.WordWrap
        text: qsTr("Sync is off on this computer: its sync key could not be read.")
        color: Style.textWarning
        font.pixelSize: Style.fontBody
    }

    GroupHeading {
        visible: panel.dialog.available
        text: qsTr("This computer")
    }
    GroupList {
        visible: panel.dialog.available
        width: panel.columnWidth

        // What the others show beside what it says and changes, in their
        // Collaboration windows.
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Picture")
                PicturePickerButton {
                    picture: panel.dialog.available ? panel.dialog.sync.computerPicture : ({})
                    name: panel.dialog.available ? panel.dialog.sync.computerName : ""
                    onPicked: function (glyph, colour) { panel.dialog.setPicture(glyph, colour) }
                    onFileAsked: panel.dialog.askPictureFile()
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Name")
                DialogField {
                    implicitWidth: Style.fieldLongW
                    text: panel.dialog.available ? panel.dialog.sync.computerName : ""
                    onCommitted: function (v) { panel.dialog.setName(v) }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Pause during services")
                ToggleSwitch {
                    checked: panel.dialog.available && panel.dialog.sync.pauseDuringServices
                    onToggled: panel.dialog.setPause(!panel.dialog.sync.pauseDuringServices)
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Sync automatically")
                ToggleSwitch {
                    checked: panel.dialog.available && panel.dialog.sync.syncAutomatically
                    onToggled: panel.dialog.setAutomatic(!panel.dialog.sync.syncAutomatically)
                }
            }
        }
    }

    GroupHeading {
        visible: panel.dialog.available
        text: qsTr("Sync network")
    }
    GroupList {
        visible: panel.dialog.available
        width: panel.columnWidth

        // The port it takes connections on, set to match a forward on its
        // router when there is one.
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Port")
                hint: panel.dialog.available && panel.dialog.sync.portPending
                      ? qsTr("Takes effect when SeventhSlide next starts") : ""
                DialogField {
                    implicitWidth: Style.fieldShortW
                    placeholder: "8766"
                    text: panel.dialog.available && panel.dialog.sync.chosenPort > 0
                          ? String(panel.dialog.sync.chosenPort) : ""
                    onCommitted: function (v) { panel.dialog.setChosenPort(v) }
                }
            }
        }
        GroupRow {
            visible: panel.dialog.available && panel.dialog.sync.portProblem.length > 0
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.dialog.available ? panel.dialog.sync.portProblem : ""
                color: Style.textWarning
                font.pixelSize: Style.fontBody
            }
        }
        // Names or addresses others reach it at, for a computer with a port
        // forwarded on its router.
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Routable address")
                DialogField {
                    implicitWidth: Style.fieldLongW
                    placeholder: qsTr("None")
                    text: panel.dialog.available ? panel.dialog.sync.reachAddresses.join(", ") : ""
                    onCommitted: function (v) { panel.dialog.setReachAddresses(v) }
                }
            }
        }
        // Said once it matters: most computers never fill the row above in.
        GroupRow {
            visible: panel.dialog.available && panel.dialog.sync.reachAddresses.length > 0
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("Anyone on the internet can now try to connect. An editor’s key can "
                           + "change a library if it is stolen, so a computer that faces the "
                           + "internet is safer as a viewer.")
                color: Style.textHint
                font.pixelSize: Style.fontSmall
            }
        }
        // Off, it asks for no introductions, and is seen only by the computers
        // it connects to or that reach it. Not anonymity.
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Share this computer’s address")
                ToggleSwitch {
                    checked: panel.dialog.available && panel.dialog.sync.shareAddress
                    onToggled: panel.dialog.setShareAddress(!panel.dialog.sync.shareAddress)
                }
            }
        }
        // For a church whose uplink also carries its livestream: computers on
        // this network are not held to it.
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Upload speed cap")
                hint: qsTr("Megabits per second")
                DialogField {
                    implicitWidth: Style.fieldShortW
                    placeholder: qsTr("None")
                    text: panel.dialog.available ? panel.dialog.sync.uploadCap : ""
                    onCommitted: function (v) { panel.dialog.setUploadCap(v) }
                }
            }
        }
    }

    GroupHeading {
        visible: panel.dialog.available
        text: qsTr("This library")
    }
    GroupList {
        visible: panel.dialog.available
        width: panel.columnWidth

        // On puts it online (a new admin password first); off stops syncing
        // (after a confirm). The switch follows the library, so a card
        // cancelled leaves it as it was.
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Enabled")
                ToggleSwitch {
                    checked: panel.dialog.synced
                    enabled: !panel.dialog.working
                             && (panel.dialog.synced || (!!panel.dialog.session && panel.dialog.session.ready))
                    opacity: enabled ? 1 : 0.5
                    onToggled: panel.dialog.synced ? panel.dialog.askStopSyncing() : panel.dialog.putOnline()
                }
            }
        }

        GroupRow {
            Text {
                text: qsTr("Status")
                color: Style.text
                font.pixelSize: Style.fontBody
            }
            Item { Layout.fillWidth: true }
            Image {
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
                source: Icons.url("sync", !panel.dialog.synced ? Style.textFaint
                                          : panel.library.trouble ? Style.textWarning
                                          : panel.library.state === "uptodate" ? Style.runningDot
                                          : Style.textSoft, 2)
                sourceSize: Qt.size(16, 16)
            }
            Text {
                Layout.maximumWidth: 420
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignRight
                text: !panel.dialog.synced ? qsTr("Off") : panel.library.text || ""
                color: panel.dialog.synced && panel.library.trouble ? Style.textWarning : Style.textStrong
                font.pixelSize: Style.fontBody
            }
            StatusBadge {
                visible: panel.dialog.synced && panel.dialog.viewer
                text: qsTr("View only")
            }
        }

        // A record too large to sync, while the status says something passing.
        GroupRow {
            visible: panel.dialog.synced && !!panel.library.tooLarge
                     && panel.library.tooLarge !== panel.library.text
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.library.tooLarge || ""
                color: Style.textWarning
                font.pixelSize: Style.fontBody
            }
        }

        // Why no snapshot is written, while the status says something else.
        GroupRow {
            visible: panel.dialog.synced && !!panel.library.snapshotProblem
                     && panel.library.snapshotProblem !== panel.library.text
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.library.snapshotProblem || ""
                color: Style.textWarning
                font.pixelSize: Style.fontBody
            }
        }

        GroupRow {
            visible: panel.dialog.synced
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Sync with the other computers")
                AppButton {
                    size: "control"
                    enabled: !panel.dialog.syncing
                    label: qsTr("Sync Now")
                    iconSource: Icons.url("sync", Style.textSoft, 2)
                    onClicked: panel.dialog.syncNow()
                }
            }
        }
    }

    // The library's computers, in a well as the lists on the other pages are:
    // each one's name, its role and when it was heard from, then the invites
    // waiting for theirs. Over it: editing the one picked (where it is reached
    // from here and, for an admin, its role) and, for an admin, removing it
    // (withdrawing an invite) and inviting another.
    GroupHeading {
        visible: panel.dialog.synced
        text: qsTr("Computers")
        tools: [
            AppButton {
                size: "control"
                iconSource: Icons.url("edit", ink)
                enabled: panel.dialog.canEditSelected && !panel.dialog.working
                onClicked: panel.dialog.editSelected()
            },
            AppButton {
                visible: panel.dialog.canAdmin
                size: "control"
                iconSource: Icons.url("trash", ink)
                enabled: panel.dialog.canRemoveSelected && !panel.dialog.working
                onClicked: panel.dialog.removeSelected()
            },
            AppButton {
                id: addButton
                visible: panel.dialog.canAdmin
                size: "control"
                primary: true
                label: qsTr("Add")
                iconSource: Icons.url("plus", ink)
                enabled: !panel.dialog.working
                onClicked: addMenu.visible ? addMenu.close() : addMenu.openAt()

                AppMenu {
                    id: addMenu
                    anchorItem: addButton
                    alignRight: true
                    minWidth: 280
                    MenuItemRow {
                        icon: "laptop"
                        label: qsTr("Invite an Editor…")
                        detail: qsTr("Changes anything in the library")
                        onTriggered: {
                            addMenu.close()
                            panel.dialog.invite("editor")
                        }
                    }
                    MenuItemRow {
                        icon: "laptop"
                        label: qsTr("Invite a Viewer…")
                        detail: qsTr("Only receives and presents: a stage display")
                        onTriggered: {
                            addMenu.close()
                            panel.dialog.invite("viewer")
                        }
                    }
                }
            }
        ]
    }
    ListWell {
        visible: panel.dialog.synced
        width: panel.columnWidth
        height: computersPane.implicitHeight + 2

        WellPane {
            id: computersPane
            Layout.fillWidth: true
            Layout.fillHeight: true
            showHeader: false

            TableHeaderBar {
                TableColumnHead {
                    Layout.fillWidth: true
                    label: qsTr("Name")
                }
                TableGroove {}
                TableColumnHead {
                    Layout.preferredWidth: panel.roleW
                    label: qsTr("Role")
                }
                TableGroove {}
                TableColumnHead {
                    Layout.preferredWidth: panel.statusW + 6
                    label: qsTr("Status")
                }
            }

            // Inset and apart, as a well's list rows are.
            Column {
                id: computerRows
                Layout.fillWidth: true
                padding: 6
                spacing: 3

                Repeater {
                    model: panel.dialog.synced ? panel.dialog.computers : []
                    delegate: TableRow {
                        id: computerRow
                        required property var modelData
                        readonly property bool removed: !!computerRow.modelData.removed
                        width: computerRows.width - computerRows.leftPadding - computerRows.rightPadding
                        implicitHeight: Style.groupRowH
                        selected: panel.dialog.selectedKey === computerRow.modelData.key
                        onClicked: panel.dialog.selectedKey = computerRow.modelData.key
                        onDoubleClicked: {
                            panel.dialog.selectedKey = computerRow.modelData.key
                            panel.dialog.editSelected()
                        }

                        // Its picture, then its name.
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            opacity: computerRow.removed ? Style.offOpacity : 1
                            Avatar {
                                id: computerPicture
                                anchors.left: parent.left
                                anchors.leftMargin: 8
                                anchors.verticalCenter: parent.verticalCenter
                                size: 20
                                // An invite's computer is not known yet: no picture of its own.
                                picture: computerRow.modelData.invite ? ({ glyph: "person", colour: Style.textCaption })
                                                                      : computerRow.modelData.picture
                                name: computerRow.modelData.name
                            }
                            Text {
                                anchors.left: computerPicture.right
                                anchors.right: parent.right
                                anchors.leftMargin: 8
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: computerRow.modelData.name
                                color: Style.textBright
                                font.pixelSize: Style.fontBody
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                        }
                        TableGroove {}
                        TableTextCell {
                            Layout.preferredWidth: panel.roleW
                            text: computerRow.modelData.role === "viewer" ? qsTr("Viewer") : qsTr("Editor")
                            ink: Style.textSoft
                            dim: computerRow.removed
                        }
                        TableGroove {}
                        TableTextCell {
                            Layout.preferredWidth: panel.statusW
                            text: panel.computerStatus(computerRow.modelData)
                            ink: computerRow.modelData.connected ? Style.textSoft : Style.textCaption
                        }
                    }
                }
            }
        }
    }

    GroupHeading {
        visible: panel.dialog.canAdmin
        text: qsTr("Admin")
    }
    GroupList {
        visible: panel.dialog.canAdmin
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Size limit")
                Row {
                    spacing: 6
                    DialogField {
                        id: limitField
                        implicitWidth: Style.fieldShortW
                        placeholder: qsTr("GB")
                        onEdited: function (v) { limitField.text = v }
                    }
                    AppButton {
                        size: "control"
                        label: qsTr("Set…")
                        enabled: !panel.dialog.working && limitField.text.trim().length > 0
                                 && !isNaN(Number(limitField.text))
                        onClicked: panel.dialog.setLimit(Number(limitField.text))
                    }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Utilization")
                Text {
                    text: panel.library.sizeLimit > 0
                          ? qsTr("%1 of %2 GB used")
                            .arg(((panel.library.sizeUsed || 0) / 1e9).toLocaleString(Qt.locale(), "f", 1))
                            .arg((panel.library.sizeLimit / 1e9).toLocaleString(Qt.locale(), "f", 1))
                          : qsTr("%1 GB used, no limit")
                            .arg(((panel.library.sizeUsed || 0) / 1e9).toLocaleString(Qt.locale(), "f", 1))
                    color: Style.textStrong
                    font.pixelSize: Style.fontBody
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Admin password")
                AppButton {
                    id: passwordButton
                    size: "control"
                    label: qsTr("Manage")
                    splitMenu: true
                    menuOpen: passwordMenu.visible
                    enabled: !panel.dialog.working
                    onClicked: passwordMenu.visible ? passwordMenu.close() : passwordMenu.openAt()
                    onMenuClicked: passwordMenu.visible ? passwordMenu.close() : passwordMenu.openAt()

                    AppMenu {
                        id: passwordMenu
                        anchorItem: passwordButton
                        alignRight: true
                        minWidth: 260
                        MenuItemRow {
                            label: qsTr("Change Password…")
                            onTriggered: {
                                passwordMenu.close()
                                panel.dialog.changePassword()
                            }
                        }
                        MenuItemRow {
                            label: qsTr("Forgotten Password…")
                            detail: qsTr("Set a new one with the recovery code")
                            onTriggered: {
                                passwordMenu.close()
                                panel.dialog.recoverPassword()
                            }
                        }
                        MenuItemRow {
                            label: qsTr("New Recovery Code…")
                            detail: qsTr("The old code stops working")
                            onTriggered: {
                                passwordMenu.close()
                                panel.dialog.newRecoveryCode()
                            }
                        }
                    }
                }
            }
        }
    }
}
