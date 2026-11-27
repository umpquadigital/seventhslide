pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The selected verse's picker, over the lyrics well's verses: its type, and
// its number among that type's, in one menu.
DropdownField {
    id: versePicker
    /// The song being edited.
    required property SongEditState dialog

    implicitWidth: 132
    enabled: versePicker.dialog.hasVerseSelection
    text: versePicker.dialog.hasVerseSelection
          ? SongText.verseDisplayName(versePicker.dialog.verses[versePicker.dialog.selectedVerse].label)
          : qsTr("No verse")
    open: verseMenu.visible
    onClicked: verseMenu.visible ? verseMenu.close() : verseMenu.openAt()

    // The type list and the number in one menu. A number another verse of
    // the type has swaps with it; one past another type's moves to the
    // next free number.
    AppMenu {
        id: verseMenu
        anchorItem: versePicker
        minWidth: 220
        onOpened: numberField.text = String(versePicker.dialog.selectedVerseNumber)

        MenuHeading {
            text: qsTr("Type")
        }
        Repeater {
            model: SongText.verseTypes
            delegate: MenuItemRow {
                required property string modelData
                required property int index
                checkable: true
                checked: index === versePicker.dialog.selectedTypeIndex
                label: modelData
                onTriggered: {
                    verseMenu.close()
                    versePicker.dialog.setSelectedVerseType(index)
                }
            }
        }
        MenuRule {}
        Item {
            width: parent ? parent.width : 0
            implicitWidth: numberRow.implicitWidth + 20
            implicitHeight: numberRow.implicitHeight + 12
            RowLayout {
                id: numberRow
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Text {
                    text: qsTr("Number")
                    color: Style.text
                    font.pixelSize: Style.fontBody
                }
                DialogField {
                    id: numberField
                    Layout.preferredWidth: 44
                    horizontalAlignment: TextInput.AlignHCenter
                    validator: IntValidator { bottom: 1; top: 99 }
                    onSubmitted: function (v) {
                        const n = parseInt(v, 10)
                        if (n)
                            versePicker.dialog.setSelectedVerseNumber(n)
                        verseMenu.close()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    readonly property var taken: versePicker.dialog.hasVerseSelection
                        ? versePicker.dialog.verseNumbersInUse(
                              versePicker.dialog.verses[versePicker.dialog.selectedVerse].label)
                        : []
                    text: taken.length ? qsTr("In use: %1").arg(taken.join(", "))
                                       : qsTr("None other in use")
                    color: Style.textCaption
                    font.pixelSize: Style.fontSmall
                    elide: Text.ElideRight
                }
            }
        }
    }
}
