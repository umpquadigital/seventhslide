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
        text: qsTr("This page holds the service’s title, date, tags and audio fades.")
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
        // Day, month and year in fields of their own, in the locale's order
        // (en_US: MM / DD / YYYY). A full field, or a separator typed ("/",
        // ".", "-", a space), moves on to the next, which is selected so
        // typing replaces it; so is a field clicked into.
        SettingRow {
            label: qsTr("Date")
            Row {
                id: dateRow
                spacing: 6
                /// The date's fields, in the order shown.
                property var fields: []
                /// True while focusPart moves the caret, which selects at once.
                property bool _moving: false

                /// Into field ``index``, all of it selected; none past the last.
                function focusPart(index) {
                    const next = dateRow.fields[index]
                    if (!next)
                        return
                    dateRow._moving = true
                    next.focusInput()
                    dateRow._moving = false
                    next.selectAll()
                }

                Repeater {
                    model: page.dialog.dateOrder.split("")
                    delegate: Row {
                        id: part
                        required property string modelData
                        required property int index
                        readonly property bool isYear: part.modelData === "Y"
                        spacing: 6

                        Text {
                            visible: part.index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.dialog.dateSeparator
                            color: Style.textCaption
                            font.pixelSize: Style.fontBody
                        }
                        DialogField {
                            id: partField
                            objectName: "serviceDate" + part.modelData
                            width: part.isYear ? 60 : 44
                            maximumLength: part.isYear ? 4 : 2
                            validator: RegularExpressionValidator { regularExpression: /\d*/ }
                            inputMethodHints: Qt.ImhDigitsOnly
                            nextKeys: "/.-, " + page.dialog.dateSeparator
                            placeholder: part.modelData === "D" ? qsTr("DD")
                                       : part.modelData === "M" ? qsTr("MM") : qsTr("YYYY")
                            text: page.dialog.datePart(part.modelData)
                            Component.onCompleted: dateRow.fields[part.index] = partField
                            onEdited: function (v) {
                                page.dialog.setDatePart(part.modelData, v)
                                if (v.length === partField.maximumLength)
                                    dateRow.focusPart(part.index + 1)
                            }
                            // 4/3/2026 typed straight through: "/" ends the 4.
                            onNextTyped: {
                                if (page.dialog.datePart(part.modelData).length)
                                    dateRow.focusPart(part.index + 1)
                            }
                            // Clicked or tabbed into: selected once the click has
                            // placed the caret, unless something was typed first
                            // (it would select that digit, and the next replace it).
                            // Not when moved to, which selects at once.
                            onInputFocusedChanged: {
                                if (!partField.inputFocused || dateRow._moving)
                                    return
                                const held = partField.text
                                Qt.callLater(function () {
                                    if (partField.inputFocused && partField.text === held)
                                        partField.selectAll()
                                })
                            }
                            // Written out in full once typed: 4 becomes 04, 26 becomes 2026.
                            onCommitted: function (v) {
                                page.dialog.setDatePart(part.modelData,
                                                        page.dialog.tidyDatePart(part.modelData, v))
                            }
                        }
                    }
                }
            }
        }
        SettingRow {
            visible: page.dialog.dateTyped
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
