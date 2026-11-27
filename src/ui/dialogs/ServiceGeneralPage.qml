pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// General page of Service Settings: the service's title, when it is held,
// its tags (which group it in the Services window), and how its audio fades.
// The fields are ServiceGeneralState's drafts, kept on Save.
ScrollPage {
    id: page

    required property ServiceGeneralState dialog

    /// The cursor in the title: a new service is named first.
    function focusTitle() {
        titleField.focusInput()
    }

    PageHeader {
        width: page.columnWidth
        title: qsTr("General")
        text: qsTr("The service’s title, when it is held, and its tags. The Services window "
                   + "lists services by date and groups them by tag. Audio fades left empty "
                   + "follow each output’s own.")
    }

    GroupHeading {
        text: qsTr("Service")
    }
    GroupList {
        width: page.columnWidth

        SettingRow {
            label: qsTr("Title")
            DialogField {
                id: titleField
                implicitWidth: Style.fieldLongW
                text: page.dialog.draftName
                placeholder: qsTr("Service title")
                onEdited: function (v) { page.dialog.draftName = v }
            }
        }
        SettingRow {
            label: qsTr("Date")
            DateField {
                implicitWidth: Style.fieldLongW
                date: page.dialog.draftDate
                onPicked: function (date) { page.dialog.draftDate = date }
            }
        }
        SettingRow {
            visible: page.dialog.draftDate.length > 0
            label: qsTr("Time")
            DialogField {
                implicitWidth: Style.fieldShortW
                text: page.dialog.draftTimeText
                placeholder: qsTr("11:00 AM")
                onEdited: function (v) { page.dialog.draftTimeText = v }
            }
        }
        SettingRow {
            label: qsTr("Tags")
            TagField {
                implicitWidth: Style.fieldLongW
                tags: page.dialog.draftTags
                placeholder: qsTr("Add tag…")
                onAdded: function (text) { page.dialog.addTag(text) }
                onRemoved: function (index) { page.dialog.removeTag(index) }
            }
        }
    }

    GroupHeading {
        text: qsTr("Audio")
    }
    GroupList {
        width: page.columnWidth

        // How every audio item fades while this service is open. One left
        // empty follows each output's own; an audio file's own come first.
        Repeater {
            model: [
                { key: "fade_in_ms", label: qsTr("Fade in") },
                { key: "fade_out_ms", label: qsTr("Fade out") },
                { key: "overlap_ms", label: qsTr("Overlap") }
            ]
            delegate: SettingRow {
                id: fadeRow
                required property var modelData
                label: fadeRow.modelData.label
                Row {
                    spacing: 8
                    DialogField {
                        objectName: "serviceAudio_" + fadeRow.modelData.key
                        width: 70
                        text: page.dialog.draftFades[fadeRow.modelData.key]
                        placeholder: qsTr("Output")
                        validator: IntValidator { bottom: 0; top: 60000 }
                        onEdited: function (v) { page.dialog.setFade(fadeRow.modelData.key, v) }
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
        SettingRow {
            label: qsTr("Fades apply to play/pause")
            OptionField {
                objectName: "serviceAudio_on_play_pause"
                implicitWidth: Style.fieldShortW
                options: [
                    { value: "", label: qsTr("Output’s") },
                    { value: "on", label: qsTr("On") },
                    { value: "off", label: qsTr("Off") }
                ]
                value: page.dialog.draftFades.on_play_pause
                onPicked: function (v) { page.dialog.setFade("on_play_pause", v) }
            }
        }
    }
}
