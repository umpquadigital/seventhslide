pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// Destinations page of the Output window: where this output's picture goes,
// as one dark well split in two, as Settings > Libraries & Profiles is. Over
// the well, as Settings' Stacks has them: Up and Down for the one selected,
// then Delete, and New, a menu of the types that says why one cannot be used
// here. On the left of the well the destinations, each with its type, and a
// switch to stop sending to it without losing its setup. On the right, under
// its type, the settings of the one selected (display, NDI, virtual camera,
// Blackmagic) and its format (the size it is sent at; an NDI source can be
// sound alone).
//
// The destinations are OutputDestinationsState (`dialog`). Each target picker
// (DestinationPicker) owns its menu.
Item {
    id: panel

    /// The destinations' state. Every `panel.dialog.` below is this panel's
    /// interface to it.
    required property OutputDestinationsState dialog

    /// The destination whose settings show, or null; and its type.
    readonly property var dest: panel.dialog.selectedDestination()
    readonly property string destType: (panel.dest && panel.dest.type) || ""
    /// A field's or picker's width on the settings island: the row less its
    /// insets (22) and a label column of about 100.
    readonly property real controlW: Math.max(Style.fieldShortW, settings.width - 22 - 12 - 100)

    /// Why type ``type`` cannot be added here, or "" when it can.
    function unavailableReason(type) {
        if (panel.dialog.destTypeAvailable(type))
            return ""
        if (type === "bmd")
            return qsTr("Install Blackmagic Desktop Video to use a DeckLink card.")
        if (type === "ndi")
            return qsTr("Install the NDI® runtime to send over the network.")
        if (type === "vcam")
            return qsTr("Needs v4l2loopback. Load it, then reopen this window.")
        if (type === "srt")
            return qsTr("This build's FFmpeg was made without SRT (libsrt).")
        return qsTr("Not available on this computer.")
    }

    /// Move the destination selected up (-1) or down (1), keeping it in view.
    function moveSelected(step) {
        const id = panel.dialog.selectedDestId
        panel.dialog.moveDestination(id, step)
        destList.positionViewAtIndex(panel.dialog.destinationIndex(id), ListView.Contain)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Destinations")
            text: qsTr("Where this output’s picture goes: a monitor or projector, a window on "
                       + "the desktop, an NDI® source on the network, an SRT stream, a Blackmagic "
                       + "card or a virtual camera.")
        }

        // Up and Down for the one selected, then Delete and New, over the well.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            AppButton {
                objectName: "destinationMoveUp"
                size: "control"
                iconSource: Icons.url("up", ink)
                enabled: panel.dialog.canMoveDestination(panel.dialog.selectedDestId, -1)
                onClicked: panel.moveSelected(-1)
            }
            AppButton {
                objectName: "destinationMoveDown"
                size: "control"
                iconSource: Icons.url("down", ink)
                enabled: panel.dialog.canMoveDestination(panel.dialog.selectedDestId, 1)
                onClicked: panel.moveSelected(1)
            }
            Item { Layout.fillWidth: true }
            AppButton {
                size: "control"
                iconSource: Icons.url("trash", ink)
                enabled: panel.dest !== null
                onClicked: panel.dialog.removeDestination(panel.dest.id)
            }
            AppButton {
                id: newButton
                size: "control"
                primary: true
                label: qsTr("New")
                iconSource: Icons.url("plus", ink)
                onClicked: newMenu.visible ? newMenu.close() : newMenu.openAt()

                AppMenu {
                    id: newMenu
                    anchorItem: newButton
                    minWidth: 260
                    Repeater {
                        model: panel.dialog.destAddChoices()
                        delegate: MenuItemRow {
                            required property var modelData
                            icon: modelData.icon
                            label: modelData.label
                            enabled: panel.dialog.destTypeAvailable(modelData.type)
                            detail: enabled ? modelData.hint : panel.unavailableReason(modelData.type)
                            onTriggered: {
                                newMenu.close()
                                panel.dialog.addDestination(modelData.type)
                            }
                        }
                    }
                }
            }
        }

        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: -4

            // ---- The destinations ----
            WellPane {
                id: listPane
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(190, Math.floor(panel.width * 0.36))
                title: qsTr("Sending To")

                // Inset in the well as the Library's lists are, the scrollbar
                // in the inset beside the rows.
                ListView {
                    id: destList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 6
                    rightMargin: Style.scrollGutter(destList)
                    spacing: 3
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.dialog.destinations
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: PickRow {
                        id: destRow
                        required property var modelData
                        readonly property bool missing: !!destRow.modelData.missing
                                                        && !!destRow.modelData.enabled
                        title: panel.dialog.destTitle(destRow.modelData)
                        // Its type; a problem in its place.
                        subtitle: destRow.missing
                                  ? qsTr("Not connected")
                                  : panel.dialog.destTypeLabel(destRow.modelData.type)
                        subtitleInk: destRow.missing ? Style.textWarning
                                   : (destRow.selected ? Style.textSoft : Style.textCaption)
                        iconSource: Icons.url(panel.dialog.destTypeIcon(destRow.modelData.type),
                                              destRow.selected ? Style.textBright : Style.textSoft, 2)
                        selected: destRow.modelData.id === panel.dialog.selectedDestId
                        onClicked: panel.dialog.selectDestination(destRow.modelData.id)
                        trailing: ToggleSwitch {
                            anchors.verticalCenter: parent.verticalCenter
                            checked: !!destRow.modelData.enabled
                            onToggled: panel.dialog.setDestinationEnabled(destRow.modelData.id,
                                                                         !destRow.modelData.enabled)
                        }
                    }
                }
            }

            // ---- The one selected ----
            WellPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ruleLeft: true
                // What it is; its name is in the list and the Name field.
                title: panel.dest ? panel.dialog.destTypeLabel(panel.destType) : ""

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    // Nothing to show: no destinations yet.
                    Column {
                        visible: panel.dest === null
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 16
                        spacing: 6
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("Nowhere to send this output yet")
                            color: Style.text
                            font.pixelSize: Style.fontUi
                            font.bold: true
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr("Use New to add a monitor, projector or other destination.")
                            color: Style.textCaption
                            font.pixelSize: Style.fontBody
                        }
                    }

                    Flickable {
                        id: settingsFlick
                        visible: panel.dest !== null
                        anchors.fill: parent
                        anchors.margins: 6
                        contentWidth: width - Style.scrollGutter(settingsFlick)
                        contentHeight: settings.implicitHeight + 12
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        flickableDirection: Flickable.VerticalFlick
                        ScrollBar.vertical: AppScrollBar {}

                        Column {
                            id: settings
                            x: 6
                            y: 6
                            width: settingsFlick.width - Style.scrollGutter(settingsFlick) - 12
                            // Spaced as a settings page is: its headings
                            // carry the gaps.
                            spacing: 0

                            // Its settings on an island, as Settings' pages
                            // have them, so they stand off the dark well.
                            GroupHeading {
                                topGap: 0
                                text: qsTr("Destination")
                            }
                            GroupList {
                                width: parent.width

                                SettingRow {
                                    label: qsTr("Name")
                                    DialogField {
                                        width: panel.controlW
                                        text: panel.dialog.destTitle(panel.dest)
                                        placeholder: qsTr("Destination name")
                                        onCommitted: function (v) {
                                            if (panel.dest && v !== panel.dialog.destTitle(panel.dest))
                                                panel.dialog.setDestinationName(panel.dest.id, v)
                                        }
                                    }
                                }
                                SettingRow {
                                    visible: panel.destType === "display"
                                    label: qsTr("Monitor")
                                    DestinationPicker {
                                        width: panel.controlW
                                        dialog: panel.dialog
                                        dest: panel.dest
                                        menuKind: "display"
                                        valueText: panel.dialog.destScreenLabel(panel.dest)
                                        currentKey: (panel.dest && panel.dest.screenKey) || ""
                                    }
                                }
                                SettingRow {
                                    visible: panel.destType === "vcam"
                                    label: qsTr("Device")
                                    DestinationPicker {
                                        width: panel.controlW
                                        dialog: panel.dialog
                                        dest: panel.dest
                                        menuKind: "vcam"
                                        valueText: panel.dialog.destCameraLabel(panel.dest)
                                        currentKey: (panel.dest && panel.dest.screenKey) || ""
                                    }
                                }
                                // Blackmagic needs two bindings, not one: a
                                // DeckLink output is locked to a broadcast video
                                // mode, so the device alone does not say what to
                                // send.
                                SettingRow {
                                    visible: panel.destType === "bmd"
                                    label: qsTr("Device")
                                    DestinationPicker {
                                        width: panel.controlW
                                        dialog: panel.dialog
                                        dest: panel.dest
                                        menuKind: "bmd"
                                        valueText: panel.dialog.destBmdDeviceLabel(panel.dest)
                                        currentKey: (panel.dest && panel.dest.targetKey) || ""
                                    }
                                }
                                SettingRow {
                                    visible: panel.destType === "bmd"
                                    label: qsTr("Video mode")
                                    DestinationPicker {
                                        width: panel.controlW
                                        dialog: panel.dialog
                                        dest: panel.dest
                                        menuKind: "bmdmode"
                                        valueText: panel.dialog.destBmdModeLabel(panel.dest)
                                        currentKey: (panel.dest && panel.dest.modeKey) || ""
                                    }
                                }
                                SettingRow {
                                    visible: panel.destType === "ndi"
                                    label: qsTr("Source name")
                                    DialogField {
                                        width: panel.controlW
                                        text: panel.dest ? (panel.dest.targetKey || "") : ""
                                        placeholder: qsTr("SeventhSlide Main")
                                        onCommitted: function (v) {
                                            if (panel.dest && v !== (panel.dest.targetKey || ""))
                                                panel.dialog.setDestinationNdiName(panel.dest.id, v)
                                        }
                                    }
                                }
                                // Hidden rather than disabled on a card with no
                                // keyer: offering a control that cannot work is
                                // worse than not offering it.
                                SettingRow {
                                    visible: panel.dialog.destBmdCanKey(panel.dest)
                                    label: qsTr("Key over video")
                                    ToggleSwitch {
                                        checked: !!(panel.dest && panel.dest.alpha)
                                        onToggled: panel.dialog.setDestinationAlpha(panel.dest.id,
                                                                                   !panel.dest.alpha)
                                    }
                                }
                                // Sound alone has no background to clear.
                                SettingRow {
                                    visible: panel.destType === "ndi"
                                    enabled: !panel.dialog.destAudioOnly(panel.dest)
                                    opacity: enabled ? 1 : Style.offOpacity
                                    label: qsTr("Transparent background")
                                    ToggleSwitch {
                                        checked: !!(panel.dest && panel.dest.alpha)
                                        onToggled: panel.dialog.setDestinationAlpha(panel.dest.id,
                                                                                   !panel.dest.alpha)
                                    }
                                }
                                // This computer's, not the destination's: the
                                // NDI runtime takes one for all its sources.
                                SettingRow {
                                    visible: panel.destType === "ndi"
                                    label: qsTr("Transport")
                                    DropdownField {
                                        id: transportField
                                        readonly property var choices: [
                                            { key: "auto", label: qsTr("Automatic") },
                                            { key: "rudp", label: qsTr("Reliable UDP") },
                                            { key: "udp", label: qsTr("UDP") },
                                            { key: "multi_tcp", label: qsTr("Multi-TCP") },
                                            { key: "tcp", label: qsTr("Single TCP") }
                                        ]
                                        readonly property string current:
                                            panel.dialog.session ? panel.dialog.session.ndiTransport : "auto"
                                        width: panel.controlW
                                        text: {
                                            const chosen = choices.find(c => c.key === current)
                                            return chosen ? chosen.label : choices[0].label
                                        }
                                        open: transportMenu.visible
                                        onClicked: transportMenu.visible ? transportMenu.close()
                                                                         : transportMenu.openAt()

                                        AppMenu {
                                            id: transportMenu
                                            anchorItem: transportField
                                            minWidth: transportField.width
                                            Repeater {
                                                model: transportField.choices
                                                delegate: MenuItemRow {
                                                    required property var modelData
                                                    checkable: true
                                                    checked: transportField.current === modelData.key
                                                    label: modelData.label
                                                    onTriggered: {
                                                        panel.dialog.setNdiTransport(modelData.key)
                                                        transportMenu.close()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                                // Sends while enabled whether the bar's Live
                                // is on or off. Last, whatever the type.
                                SettingRow {
                                    label: qsTr("Ignore Live")
                                    ToggleSwitch {
                                        checked: !!(panel.dest && panel.dest.ignoreLive)
                                        onToggled: panel.dialog.setDestinationIgnoreLive(panel.dest.id,
                                                                                        !panel.dest.ignoreLive)
                                    }
                                }
                            }

                            OutputStreamSettings {
                                visible: panel.destType === "srt"
                                width: parent.width
                                dialog: panel.dialog
                                dest: panel.dest
                                controlW: panel.controlW
                            }

                            // Its sound: a display's or window's own sound
                            // device, or the output's mix sent with the
                            // picture (NDI, Blackmagic, a virtual camera's
                            // microphone).
                            GroupHeading {
                                visible: panel.dialog.destHasSoundDevice(panel.dest)
                                         || !!(panel.dest && panel.dest.audioCapable)
                                text: qsTr("Audio")
                            }
                            GroupList {
                                visible: panel.dialog.destHasSoundDevice(panel.dest)
                                         || !!(panel.dest && panel.dest.audioCapable)
                                width: parent.width

                                SettingRow {
                                    visible: panel.dialog.destHasSoundDevice(panel.dest)
                                    label: qsTr("Sound")
                                    DestinationPicker {
                                        width: panel.controlW
                                        dialog: panel.dialog
                                        dest: panel.dest
                                        menuKind: "sound"
                                        valueText: panel.dialog.destSoundLabel(panel.dest)
                                        currentKey: (panel.dest && panel.dest.audioDevice) || ""
                                    }
                                }
                                // Sound alone is the sound: on while it is.
                                SettingRow {
                                    visible: !!(panel.dest && panel.dest.audioCapable)
                                    enabled: !panel.dialog.destAudioOnly(panel.dest)
                                    opacity: enabled ? 1 : Style.offOpacity
                                    label: panel.destType === "vcam" ? qsTr("Microphone")
                                                                    : qsTr("Send audio")
                                    ToggleSwitch {
                                        checked: !!panel.dest && panel.dest.audio !== false
                                        onToggled: panel.dialog.setDestinationAudio(
                                                       panel.dest.id, panel.dest.audio === false)
                                    }
                                }
                            }

                            // How it is drawn: as the output is unless switched
                            // off, then smaller in its shape, or (NDI) as sound
                            // alone. A display's smaller picture fills its
                            // screen.
                            GroupHeading {
                                visible: panel.dialog.destSizable(panel.dest)
                                text: qsTr("Format")
                            }
                            GroupList {
                                visible: panel.dialog.destSizable(panel.dest)
                                width: parent.width

                                SettingRow {
                                    label: qsTr("Match output")
                                    ToggleSwitch {
                                        checked: panel.dialog.destMatchesOutput(panel.dest)
                                        onToggled: panel.dialog.setDestinationMatchesOutput(
                                                       panel.dest.id,
                                                       !panel.dialog.destMatchesOutput(panel.dest))
                                    }
                                }
                                SettingRow {
                                    visible: panel.dialog.destCanBeAudioOnly(panel.dest)
                                    label: qsTr("Audio only")
                                    ToggleSwitch {
                                        checked: panel.dialog.destAudioOnly(panel.dest)
                                        onToggled: panel.dialog.setDestinationAudioOnly(
                                                       panel.dest.id,
                                                       !panel.dialog.destAudioOnly(panel.dest))
                                    }
                                }
                                SettingRow {
                                    label: qsTr("Size")
                                    DropdownField {
                                        id: sizeField
                                        width: panel.controlW
                                        enabled: !panel.dialog.destMatchesOutput(panel.dest)
                                                 && !panel.dialog.destAudioOnly(panel.dest)
                                        text: panel.dialog.destSizeText(panel.dest)
                                        open: sizeMenu.visible
                                        onClicked: sizeMenu.visible ? sizeMenu.close() : sizeMenu.openAt()

                                        AppMenu {
                                            id: sizeMenu
                                            anchorItem: sizeField
                                            minWidth: sizeField.width
                                            Repeater {
                                                model: panel.dialog.destSizeOptions()
                                                delegate: MenuItemRow {
                                                    required property var modelData
                                                    checkable: true
                                                    checked: {
                                                        const size = panel.dialog.destSize(panel.dest)
                                                        return !!size && size.h === modelData.h
                                                    }
                                                    label: modelData.label
                                                    shortcut: qsTr("%1 × %2").arg(modelData.w).arg(modelData.h)
                                                    onTriggered: {
                                                        panel.dialog.setDestinationSize(panel.dest.id,
                                                                                        modelData.h)
                                                        sizeMenu.close()
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // What is happening, or why not.
                            Note {
                                visible: !!panel.dest && !!panel.dest.enabled && !!panel.dest.closed
                                text: panel.dest && panel.dest.ignoreLive
                                      ? qsTr("Its window was closed. Switch it off and on to open it again.")
                                      : qsTr("Its window was closed. It opens again when Live is turned "
                                             + "on, or when it is switched off and on.")
                                color: Style.textCaption
                            }
                            Note {
                                visible: panel.destType === "display" && !!panel.dest.missing
                                         && !!panel.dest.enabled
                                text: qsTr("The monitor picked is not connected.")
                                color: Style.textWarning
                            }
                            Note {
                                visible: panel.destType === "bmd" && !!panel.dialog.bmdRuntime.message
                                         && panel.dialog.bmdRuntime.message.length > 0
                                text: panel.dialog.bmdRuntime.message
                                color: Style.textWarning
                            }
                            Note {
                                visible: panel.destType === "bmd" && !!panel.dest.live
                                         && !!panel.dest.modeName && panel.dest.modeName.length > 0
                                text: panel.dest && panel.dest.keying
                                      ? qsTr("Sending %1 with the keyer on.").arg(panel.dest.modeName)
                                      : qsTr("Sending %1.").arg(panel.dest ? panel.dest.modeName : "")
                                color: Style.textOk
                            }
                            Note {
                                visible: text.length > 0
                                text: panel.dialog.streamStatusText(panel.dest)
                                color: panel.dialog.streamStatusOk(panel.dest) ? Style.textOk
                                     : panel.dest && (panel.dest.streamState === "retrying"
                                                      || panel.dest.streamState === "failed")
                                       ? Style.textWarning : Style.textCaption
                            }
                            Note {
                                visible: panel.destType === "ndi"
                                         && panel.dialog.destReceiversText(panel.dest).length > 0
                                text: panel.dialog.destReceiversText(panel.dest)
                                color: Style.textOk
                            }
                            Note {
                                visible: panel.destType === "ndi" && !!panel.dialog.session
                                         && panel.dialog.session.ndiTransportPending
                                text: qsTr("Restart SeventhSlide to change the transport.")
                                color: Style.textWarning
                            }
                            Note {
                                visible: panel.destType === "ndi" && !panel.dialog.ndiRuntime.available
                                text: panel.dialog.ndiRuntime.message
                                color: Style.textWarning
                            }
                            Note {
                                visible: panel.destType === "vcam" && !!panel.dest.missing
                                // Its words are the system's own (C++): on Linux how to
                                // load v4l2loopback, elsewhere that there is none yet.
                                text: panel.dialog.cameras.length === 0
                                      ? String((panel.dialog.destTypeInfo("vcam") || {}).message || "")
                                      : qsTr("The device picked is not connected.")
                                color: Style.textWarning
                            }
                            Note {
                                visible: panel.destType === "srt" && panel.dialog.streamCodecs.length > 0
                                         && panel.dialog.streamCodecOptions().length === 0
                                text: qsTr("No H.264 or H.265 encoder works on this computer, so it cannot stream.")
                                color: Style.textWarning
                            }
                            Note {
                                visible: !!panel.dest && !panel.dest.enabled
                                         && panel.dialog.destConflictText(panel.dest).length > 0
                                text: panel.dialog.destConflictText(panel.dest)
                                color: Style.textWarning
                            }
                            // NDI is installed separately by the operator;
                            // SeventhSlide ships none of it. Attribution is
                            // required where we advertise support.
                            Note {
                                visible: panel.destType === "ndi"
                                text: panel.dialog.ndiRuntime.available
                                      ? qsTr("Using %1. NDI® is a registered trademark of Vizrt NDI AB.")
                                            .arg(panel.dialog.ndiRuntime.version)
                                      : qsTr("NDI® is a registered trademark of Vizrt NDI AB.")
                                color: Style.legalInk
                            }
                        }
                    }
                }
            }
        }
    }

    /// A line of status under a destination's settings.
    component Note: Text {
        width: parent ? parent.width : 0
        topPadding: 8
        wrapMode: Text.WordWrap
        font.pixelSize: Style.fontSmall
    }
}
