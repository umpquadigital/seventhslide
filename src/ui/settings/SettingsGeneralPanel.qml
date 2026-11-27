pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import SeventhSlide

// General page of Settings: the organization's name and CCLI licence, the
// theme priority order and the Library panel's tabs. The priorities are a
// table in a dark well, Order and Name, as Stacks is, the one picked moved by
// the arrows on its heading; the tabs are rows dragged by their glyphs. Its
// state lives on SettingsGeneralState; the drag ghost is the dialog's, drawn
// above the page.
ScrollPage {
    id: panel

    required property SettingsGeneralState dialog

    interactive: !panel.dialog.tabDrag.active

    PageHeader {
        width: panel.columnWidth
        title: qsTr("General")
        text: qsTr("Enter your organization’s name, and its CCLI license number so it shows at the "
                   + "end of each song. Theme priority decides which look wins when a song, "
                   + "the service and the default each have their own. Library tabs choose "
                   + "which tabs this computer shows, in what order and how many at once.")
    }

    GroupHeading {
        text: qsTr("Organization")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Organization name")
                DialogField {
                    implicitWidth: Style.fieldLongW
                    text: panel.dialog.organizationName
                    placeholder: qsTr("Riverside Community")
                    onEdited: function (v) { panel.dialog.organizationName = v }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("CCLI license number")
                DialogField {
                    implicitWidth: Style.fieldMediumW
                    text: panel.dialog.ccli
                    placeholder: qsTr("123456")
                    onEdited: function (v) { panel.dialog.ccli = v }
                }
            }
        }
    }

    // Up and Down for the priority picked, on its heading.
    GroupHeading {
        text: qsTr("Theme priority")
        tools: [
            AppButton {
                objectName: "priorityMoveUp"
                size: "control"
                iconSource: Icons.url("up", ink)
                enabled: panel.dialog.canMovePriority(-1)
                onClicked: panel.dialog.movePriority(-1)
            },
            AppButton {
                objectName: "priorityMoveDown"
                size: "control"
                iconSource: Icons.url("down", ink)
                enabled: panel.dialog.canMovePriority(1)
                onClicked: panel.dialog.movePriority(1)
            }
        ]
    }
    ListWell {
        width: panel.columnWidth
        // Every row in view: the bar, the rows, the list's margins and the
        // well's outline.
        height: 30 + prioList.topMargin + prioList.bottomMargin
                + panel.dialog.priority.length * Style.groupRowH
                + Math.max(0, panel.dialog.priority.length - 1) * prioList.spacing + 2

        WellPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            showHeader: false

            TableHeaderBar {
                // The places' column; the rows' inset from the well's edge
                // with it, so the grooves line up.
                TableColumnHead {
                    Layout.preferredWidth: Style.orderColumnW + prioList.leftMargin
                    label: qsTr("Order")
                }
                TableGroove {}
                TableColumnHead {
                    Layout.fillWidth: true
                    label: qsTr("Name")
                }
            }

            ListView {
                id: prioList
                Layout.fillWidth: true
                Layout.fillHeight: true
                topMargin: 6
                bottomMargin: 6
                leftMargin: 6
                rightMargin: 6
                spacing: 3
                interactive: false
                model: panel.dialog.priority

                delegate: TableRow {
                    id: prioRow
                    required property int index
                    required property string modelData

                    implicitHeight: Style.groupRowH
                    selected: prioRow.index === panel.dialog.shownPriorityIndex
                    onClicked: panel.dialog.selectedPriority = prioRow.modelData

                    // Its place, 1 the highest.
                    TableTextCell {
                        Layout.preferredWidth: Style.orderColumnW
                        text: String(prioRow.index + 1)
                        ink: Style.textSoft
                        centered: true
                    }
                    TableGroove {}
                    TableTextCell {
                        Layout.fillWidth: true
                        text: panel.dialog.priorityLabels[prioRow.modelData] || prioRow.modelData
                        ink: Style.textBright
                        bold: true
                    }
                }
            }
        }
    }

    GroupHeading {
        text: qsTr("Library tabs")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Tabs shown at once")
                OptionField {
                    objectName: "libraryTabsShown"
                    implicitWidth: Style.fieldShortW
                    options: panel.dialog.tabsShownOptions
                    value: panel.dialog.tabsShown
                    onPicked: function (v) { panel.dialog.tabsShown = v }
                }
            }
        }
    }

    // The rows are a list of their own, as the drag counts from its top; a gap
    // keeps it from the option above.
    Item {
        width: panel.columnWidth
        height: 10
    }
    GroupList {
        id: tabList
        width: panel.columnWidth

        Repeater {
            model: panel.dialog.tabOrder
            delegate: GroupRow {
                id: tabRow
                objectName: "libraryTabRow"
                required property int index
                required property string modelData
                readonly property var type: ContentTypes.ofTab(tabRow.modelData)
                readonly property string labelText: tabRow.type ? tabRow.type.label : tabRow.modelData
                readonly property string iconText: tabRow.type ? tabRow.type.icon : ""
                readonly property bool shown: panel.dialog.tabShown(tabRow.modelData)
                readonly property var drag: panel.dialog.tabDrag

                lifted: tabRow.drag.active && tabRow.drag.from === tabRow.index
                dropAbove: tabRow.drag.active && tabRow.drag.insertBefore === tabRow.index
                dropBelow: tabRow.drag.active && tabRow.index === panel.dialog.tabOrder.length - 1
                           && tabRow.drag.insertBefore === panel.dialog.tabOrder.length

                HandleTile {
                    objectName: "libraryTabHandle"
                    iconSource: Icons.url(tabRow.iconText, tabRow.shown ? Style.textSoft
                                                                        : Style.textMuted, 2)
                    draggable: true
                    ReorderMouseArea {
                        anchors.fill: parent
                        reorder: tabRow.drag
                        rows: tabList.column
                        index: tabRow.index
                        label: tabRow.labelText
                        icon: Icons.url(tabRow.iconText, Style.textSoft, 2)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: tabRow.labelText
                    color: tabRow.shown ? Style.textBright : Style.textMuted
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                // The last tab showing stays on.
                ToggleSwitch {
                    objectName: "libraryTabToggle"
                    checked: tabRow.shown
                    enabled: panel.dialog.tabToggleable(tabRow.modelData)
                    opacity: enabled ? 1 : 0.5
                    onToggled: panel.dialog.toggleTab(tabRow.modelData)
                }
            }
        }
    }
}
