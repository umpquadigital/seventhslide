pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// General page of the Output window: the output's name, canvas and icon,
// which stacks and content types it shows, how audio fades on it, and whether
// global Blank / Freeze reach it. The output is OutputEditState (`dialog`); its
// Save saves these.
ScrollPage {
    id: panel

    /// The state this shows. Every `panel.dialog.` below is its interface to it.
    required property OutputEditState dialog

    PageHeader {
        width: panel.columnWidth
        title: qsTr("General")
        text: qsTr("Give this output a name, choose its size, and pick which stacks and "
                   + "content it shows. "
                   + "A screen facing the platform can keep showing the words even when the "
                   + "main screen is blanked.")
    }

    GroupHeading {
        text: qsTr("Output")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Name")
                DialogField {
                    implicitWidth: Style.fieldLongW
                    text: panel.dialog.editName
                    placeholder: qsTr("Output name")
                    onEdited: function (v) { panel.dialog.editName = v }
                }
            }
        }
        // The canvas by name ("Custom" for a size no preset is), and its size
        // as fields under it.
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Canvas")
                DropdownField {
                    id: canvasField
                    // As wide as its widest name needs ("1080p Vertical").
                    implicitWidth: 140
                    text: panel.dialog.canvasInfo.preset || qsTr("Custom")
                    // The fields under it say the size; a custom one's shape
                    // is said here.
                    detail: panel.dialog.canvasInfo.preset ? "" : panel.dialog.canvasInfo.aspect
                    open: canvasMenu.visible
                    onClicked: canvasMenu.visible ? canvasMenu.close() : canvasMenu.openAt()

                    AppMenu {
                        id: canvasMenu
                        anchorItem: canvasField
                        minWidth: canvasField.width
                        Repeater {
                            model: panel.dialog.canvasPresetGroups
                            delegate: MenuSection {
                                id: shapeSection
                                required property var modelData
                                title: modelData.heading
                                Repeater {
                                    model: shapeSection.modelData.presets
                                    delegate: MenuItemRow {
                                        required property var modelData
                                        checkable: true
                                        checkSlot: false
                                        checked: modelData.title === panel.dialog.canvasInfo.preset
                                        label: modelData.name
                                        onTriggered: {
                                            panel.dialog.pickCanvasPreset(modelData.width,
                                                                          modelData.height)
                                            canvasMenu.close()
                                        }
                                    }
                                }
                            }
                        }
                        MenuRule {}
                        MenuItemRow {
                            checkable: true
                            checkSlot: false
                            checked: !panel.dialog.canvasInfo.preset
                            label: qsTr("Custom…")
                            onTriggered: {
                                canvasMenu.close()
                                widthField.focusInput()
                            }
                        }
                    }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Size")
                hint: panel.dialog.canvasInfo.problem
                hintInk: Style.textWarning
                Row {
                    spacing: 8
                    DialogField {
                        id: widthField
                        width: 70
                        text: panel.dialog.canvasW
                        placeholder: qsTr("Width")
                        onEdited: function (v) { panel.dialog.canvasW = v }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "×"
                        color: Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                    DialogField {
                        width: 70
                        text: panel.dialog.canvasH
                        placeholder: qsTr("Height")
                        onEdited: function (v) { panel.dialog.canvasH = v }
                    }
                    IconToolButton {
                        objectName: "canvasSwapSides"
                        size: "control"
                        icon: "swap"
                        onClicked: panel.dialog.swapCanvasSides()
                    }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Icon")
                IconPickerButton {
                    icons: panel.dialog.session ? panel.dialog.session.outputSettings.iconIds() : []
                    current: panel.dialog.editIcon
                    onPicked: function (id) { panel.dialog.editIcon = id }
                }
            }
        }
    }

    GroupHeading {
        text: qsTr("Stacks")
    }
    GroupList {
        width: panel.columnWidth

        Repeater {
            model: panel.dialog.session ? panel.dialog.session.stacks.all : []
            delegate: IconToggleRow {
                required property var modelData
                icon: modelData.icon
                label: modelData.name || modelData.id
                checked: panel.dialog.stackIsEnabled(modelData.id)
                onToggled: panel.dialog.toggleOutputStack(modelData.id)
            }
        }
    }

    GroupHeading {
        text: qsTr("Content")
    }
    GroupList {
        width: panel.columnWidth

        Repeater {
            model: ContentTypes.items
            delegate: IconToggleRow {
                required property var modelData
                icon: modelData.icon
                label: modelData.label
                checked: panel.dialog.contentTypeIsEnabled(modelData.key)
                onToggled: panel.dialog.toggleContentType(modelData.key)
            }
        }
    }

    GroupHeading {
        text: qsTr("Audio")
    }
    GroupList {
        width: panel.columnWidth

        // How an audio item's sound comes and goes here, unless the service
        // or the audio file says otherwise. A video's sound goes with its
        // picture instead, over this output's Media theme's Video transition.
        Repeater {
            model: [
                { key: "fade_in_ms", label: qsTr("Fade in") },
                { key: "fade_out_ms", label: qsTr("Fade out") },
                { key: "overlap_ms", label: qsTr("Overlap") }
            ]
            delegate: GroupRow {
                id: fadeRow
                required property var modelData
                FormRow {
                    Layout.fillWidth: true
                    label: fadeRow.modelData.label
                    Row {
                        spacing: 8
                        DialogField {
                            objectName: "outputAudio_" + fadeRow.modelData.key
                            width: 70
                            text: panel.dialog.audioFades[fadeRow.modelData.key]
                            validator: IntValidator { bottom: 0; top: 60000 }
                            onEdited: function (v) { panel.dialog.setAudioFade(fadeRow.modelData.key, v) }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("ms")
                            color: Style.textCaption
                            font.pixelSize: Style.fontBody
                        }
                    }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Fades apply to play/pause")
                ToggleSwitch {
                    objectName: "outputAudio_on_play_pause"
                    checked: panel.dialog.audioFadesOnPlayPause
                    onToggled: panel.dialog.audioFadesOnPlayPause = !panel.dialog.audioFadesOnPlayPause
                }
            }
        }
    }

    GroupHeading {
        text: qsTr("Live control")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Ignore global Blank")
                ToggleSwitch {
                    checked: panel.dialog.exemptBlank
                    onToggled: panel.dialog.exemptBlank = !panel.dialog.exemptBlank
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Ignore global Freeze")
                ToggleSwitch {
                    checked: panel.dialog.exemptFreeze
                    onToggled: panel.dialog.exemptFreeze = !panel.dialog.exemptFreeze
                }
            }
        }
    }
}
