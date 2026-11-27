pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// Web Remote page of Settings: on or off, how a phone connects (status,
// address, QR code) and who may (PIN, port). Its state lives on
// SettingsRemoteState; the switch, PIN and port are saved with the dialog.
ScrollPage {
    id: panel

    required property SettingsRemoteState dialog
    readonly property RemoteController remote: panel.dialog.remote
    readonly property bool running: panel.dialog.remoteAvailable && panel.remote.running

    PageHeader {
        width: panel.columnWidth
        title: qsTr("Web Remote")
        text: qsTr("Run the slides from a phone or tablet. Connect it to the same Wi-Fi as "
                   + "this computer, then scan the QR code below or open the address in its "
                   + "web browser.")
    }

    GroupHeading {
        text: qsTr("Connection")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Enabled")
                ToggleSwitch {
                    checked: panel.dialog.remoteOn
                    onToggled: panel.dialog.remoteOn = !panel.dialog.remoteOn
                }
            }
        }

        GroupRow {
            Text {
                Layout.fillWidth: true
                text: qsTr("Status")
                color: Style.text
                font.pixelSize: Style.fontBody
            }
            Rectangle {
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: panel.running ? Style.runningDot
                     : (panel.dialog.remoteAvailable && panel.remote.lastError.length
                        ? Style.textDanger : Style.textFaint)
            }
            Text {
                Layout.maximumWidth: 300
                elide: Text.ElideRight
                color: Style.textStrong
                font.pixelSize: Style.fontBody
                text: {
                    if (!panel.dialog.remoteAvailable)
                        return qsTr("Unavailable in this build")
                    if (panel.remote.running)
                        return qsTr("Running")
                    if (panel.remote.lastError.length)
                        return panel.remote.lastError
                    return panel.dialog.remoteOn ? qsTr("Starts when you save")
                                                 : qsTr("Off")
                }
            }
        }

        GroupRow {
            visible: panel.running
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Address")
                Row {
                    spacing: 6
                    // The address, or a choice of them on a computer on more
                    // than one network.
                    Rectangle {
                        id: addressField
                        readonly property bool several: panel.dialog.addresses.length > 1
                        width: Style.fieldLongW
                        height: Style.controlH
                        radius: Style.radiusSmall
                        color: Style.fieldFace
                        border.color: addressMenu.visible ? Style.focusEdge
                                    : (addressArea.containsMouse && addressField.several
                                       ? Style.controlEdgeHover : Style.controlEdge)
                        Text {
                            anchors.left: parent.left
                            anchors.right: addressCaret.visible ? addressCaret.left : parent.right
                            anchors.leftMargin: 9
                            anchors.rightMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            text: panel.dialog.address
                            color: Style.linkInk
                            font.pixelSize: Style.fontBody
                            font.family: Platform.monoFamily
                            elide: Text.ElideRight
                        }
                        Image {
                            id: addressCaret
                            visible: addressField.several
                            anchors.right: parent.right
                            anchors.rightMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                            width: 10
                            height: 10
                            rotation: addressMenu.visible ? 180 : 0
                            source: Icons.url("chevronDown", Style.chevronInk, 2.5)
                            sourceSize: Qt.size(10, 10)
                        }
                        MouseArea {
                            id: addressArea
                            anchors.fill: parent
                            enabled: addressField.several
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: addressMenu.visible ? addressMenu.close() : addressMenu.openAt()
                        }
                        AppMenu {
                            id: addressMenu
                            anchorItem: addressField
                            minWidth: addressField.width
                            Repeater {
                                model: panel.dialog.addresses
                                delegate: MenuItemRow {
                                    required property string modelData
                                    required property int index
                                    checkable: true
                                    checked: modelData === panel.dialog.address
                                    label: modelData
                                    onTriggered: {
                                        panel.dialog.addressIndex = index
                                        addressMenu.close()
                                    }
                                }
                            }
                        }
                    }
                    IconToolButton {
                        size: "control"
                        icon: "copy"
                        onClicked: Clipboard.setText(panel.dialog.address)
                    }
                }
            }
        }

        GroupRow {
            visible: panel.running && panel.dialog.address.length > 0
            Text {
                Layout.fillWidth: true
                text: qsTr("QR code")
                color: Style.text
                font.pixelSize: Style.fontBody
            }
            // A whole number of pixels per module keeps the code sharp.
            Image {
                id: qr
                readonly property int modules: QrCode.extent(panel.dialog.address)
                readonly property int side: qr.modules * Math.max(2, Math.floor(112 / Math.max(1, qr.modules)))
                Layout.preferredWidth: qr.side
                Layout.preferredHeight: qr.side
                Layout.topMargin: 2
                Layout.bottomMargin: 2
                source: QrCode.url(panel.dialog.address)
                sourceSize: Qt.size(qr.side, qr.side)
                smooth: false
            }
        }

        GroupRow {
            visible: !panel.running
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Style.textCaption
                font.pixelSize: Style.fontBody
                text: panel.dialog.remoteAvailable && panel.dialog.remoteOn
                      && !panel.remote.lastError.length
                      ? qsTr("The address and QR code show here once the remote is running.")
                      : qsTr("Turn the remote on and save to get an address for your phone.")
            }
        }
    }

    GroupHeading {
        text: qsTr("Access")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("PIN")
                hint: qsTr("Leave blank for none.")
                DialogField {
                    implicitWidth: Style.fieldShortW
                    text: panel.dialog.remotePin
                    password: true
                    onEdited: function (v) { panel.dialog.remotePin = v }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Port")
                DialogField {
                    implicitWidth: Style.fieldShortW
                    text: String(panel.dialog.remotePort)
                    onEdited: function (v) {
                        // Out of range (or not a number) is the default, as saving makes it.
                        panel.dialog.remotePort = panel.dialog.session.normalizeRemotePort(parseInt(v, 10) || 0)
                    }
                }
            }
        }
    }

    Text {
        width: panel.columnWidth
        topPadding: 12
        wrapMode: Text.WordWrap
        text: qsTr("The remote is served over plain HTTP on your local network. The PIN "
                   + "controls who may connect; it does not encrypt anything.")
        color: Style.noteInk
        font.pixelSize: Style.fontSmall
    }
}
