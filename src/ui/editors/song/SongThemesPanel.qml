pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// Themes page of the song editor: one well split into the theme changes in
// the play order (left) and, for the one picked, its text and background on
// each screen (right). The song is SongEditState (`dialog`); the slots edit
// the dialog's ThemeMapEditor, which hands the picked change's map back.
Item {
    id: panel

    /// The song editor's state. Every `panel.dialog.` below is this panel's
    /// interface to it.
    required property SongEditState dialog
    /// ``dialog.session``, typed, so calls on it are checked.
    readonly property SessionFacade session: panel.dialog.session

    /// The picked change's place in the list, or -1.
    readonly property int selectedIndex: {
        const list = panel.dialog.themeChipList
        for (let i = 0; i < list.length; ++i) {
            if (list[i].code === String(panel.dialog.selectedThemeCode).toUpperCase())
                return i
        }
        return -1
    }

    /// What plays under the change at ``orderIndex``, until the next one:
    /// "Then Title, V1, C1".
    function followsText(orderIndex) {
        const codes = panel.dialog.chipsAfterTheme(orderIndex).map(function (c) {
            return SongText.isTitleCode(c) ? qsTr("Title") : String(c)
        })
        return codes.length ? qsTr("Then %1").arg(codes.join(", "))
                            : qsTr("Nothing plays after it yet")
    }

    function pick(code, orderIndex) {
        panel.dialog.flushSelectedChipMap()
        panel.dialog.selectedOrderIndex = orderIndex
        panel.dialog.syncThemeMapFromChip(code)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Themes")
            text: qsTr("Theme changes let a song’s look change part-way through.")
        }

        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true

            WellPane {
                Layout.fillHeight: true
                Layout.preferredWidth: Math.max(220, Math.floor(panel.width * 0.4))
                headerHeight: Style.wellBarH
                title: qsTr("Theme changes")
                tools: [
                    AppButton {
                        size: "control"
                        iconSource: Icons.url("plus", ink)
                        onClicked: {
                            panel.dialog.flushSelectedChipMap()
                            panel.dialog.addThemeChip()
                        }
                    },
                    AppButton {
                        size: "control"
                        iconSource: Icons.url("trash", ink)
                        enabled: panel.selectedIndex >= 0
                        onClicked: panel.dialog.removeSelectedThemeChip()
                    }
                ]

                ListView {
                    id: changeList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The bar a gap in from the well's edge, as the Library's is.
                    Layout.rightMargin: Style.scrollGap
                    topMargin: 6
                    bottomMargin: 6
                    leftMargin: 6
                    rightMargin: Style.scrollGutter(changeList)
                    spacing: 3
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.dialog.themeChipList
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: PickRow {
                        id: change
                        required property int index
                        required property var modelData
                        code: panel.dialog.chipLabel(change.modelData.code)
                        codeInk: Style.chipThemeInk
                        title: qsTr("Theme change %1").arg(change.index + 1)
                        subtitle: panel.followsText(Number(change.modelData.orderIndex))
                        selected: change.index === panel.selectedIndex
                        onClicked: panel.pick(String(change.modelData.code),
                                              Number(change.modelData.orderIndex))
                    }

                    Text {
                        visible: changeList.count === 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("No theme changes yet. Use + here, or + after the order on "
                                   + "the Lyrics page.")
                        color: Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                }
            }

            WellPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ruleLeft: true
                headerHeight: Style.wellBarH
                title: panel.selectedIndex >= 0
                       ? qsTr("Theme change %1").arg(panel.selectedIndex + 1) : ""

                Flickable {
                    id: screensFlick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The bar a gap in from the well's edge, as the Library's
                    // is; the blocks stay 12 in from it without one.
                    Layout.rightMargin: Style.scrollGap
                    visible: panel.selectedIndex >= 0
                    contentWidth: width - Style.scrollGutter(screensFlick)
                    contentHeight: screens.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick
                    ScrollBar.vertical: AppScrollBar {}

                    Column {
                        id: screens
                        width: screensFlick.width - Style.scrollGutter(screensFlick)
                        padding: 12
                        rightPadding: 12 - Style.scrollGap
                        spacing: 10

                        Text {
                            visible: screenRepeater.count === 0
                            text: qsTr("No outputs yet.")
                            color: Style.textCaption
                            font.pixelSize: Style.fontBody
                        }

                        // One block per screen: its name, then its text and
                        // background side by side (ThemeMapEditor.slotGroups).
                        Repeater {
                            id: screenRepeater
                            model: panel.session ? panel.session.outputs : null
                            delegate: ScreenThemeSlots {
                                id: screen
                                required property string name
                                width: screens.width - screens.leftPadding - screens.rightPadding
                                map: panel.dialog.themeMapEditor
                                outputName: screen.name
                            }
                        }
                    }
                }

                Text {
                    visible: panel.selectedIndex < 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.margins: 20
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Pick a theme change to set its look on each screen.")
                    color: Style.textCaption
                    font.pixelSize: Style.fontBody
                }
            }
        }
    }
}
