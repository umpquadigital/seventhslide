pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// Stacks page of Settings, laid out as Bibles is: a dark well holding a table
// of the stacks front to back, Order, Icon, Name, Default Reception and P/P
// (whether it has a Preview), and over it arrows that move the picked stack up
// and down, then Delete and New. Each row leads with its place (1 in front); a
// double click on the name edits it in place, and its icon, what it receives
// and whether it has a Preview are set in their cells. Its state and actions
// live on SettingsStacksState; the confirms are the dialog's.
Item {
    id: panel

    required property SettingsStacksState dialog
    /// ``dialog.session``, typed, so calls on it are checked.
    readonly property SessionFacade session: panel.dialog.session
    /// The stack whose name is open for editing, or "".
    property string editingId: ""
    /// The columns but Order and Name, header and rows alike, padding and all.
    readonly property int receivesW: 175
    /// The icon picker with a little room either side, as Outputs' icons.
    readonly property int iconW: 58
    readonly property int previewW: 72

    /// Close the name open, keeping ``text`` when ``keep``. Kept a moment
    /// later: the list rebuilding its rows closes an edit, and a rename then
    /// would change the list in the middle of that.
    function endEdit(keep, text) {
        const id = panel.editingId
        if (!id.length)
            return
        panel.editingId = ""
        if (keep)
            Qt.callLater(panel.dialog.renameStack, id, text)
    }

    /// Move the stack picked ``step`` rows (-1 up, 1 down), keeping it in view.
    function moveShown(step) {
        const id = panel.dialog.shownStackId
        panel.dialog.moveStack(id, step)
        stackList.positionViewAtIndex(panel.dialog.stackDisplayIndex(id), ListView.Contain)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Stacks")
            text: qsTr("Stacks let you show more than one thing on screen at once, like a Bible "
                       + "verse along the bottom while a picture stays behind it. Stacks higher in "
                       + "the list show in front. Each new item goes to the stack set to receive "
                       + "it, or to %1.").arg(panel.dialog.stackName(panel.dialog.fallbackId))
        }

        // Up and Down for the stack picked, then Delete and New, over the well.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            AppButton {
                objectName: "stackMoveUp"
                size: "control"
                iconSource: Icons.url("up", ink)
                enabled: panel.dialog.canMoveStack(panel.dialog.shownStackId, -1)
                onClicked: panel.moveShown(-1)
            }
            AppButton {
                objectName: "stackMoveDown"
                size: "control"
                iconSource: Icons.url("down", ink)
                enabled: panel.dialog.canMoveStack(panel.dialog.shownStackId, 1)
                onClicked: panel.moveShown(1)
            }
            Item { Layout.fillWidth: true }
            AppButton {
                size: "control"
                iconSource: Icons.url("trash", ink)
                enabled: panel.dialog.stackRows.length > 1 && panel.dialog.shownStackId.length > 0
                onClicked: panel.dialog.beginDeleteStack(panel.dialog.shownStackId)
            }
            AppButton {
                size: "control"
                primary: true
                label: qsTr("New")
                iconSource: Icons.url("plus", ink)
                enabled: !!panel.session
                onClicked: panel.dialog.addNewStack()
            }
        }

        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: -4

            WellPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                showHeader: false

                TableHeaderBar {
                    rightInset: Style.scrollGutter(stackList)

                    // The places' column; the rows' inset from the well's
                    // edge with it, so the grooves line up.
                    TableColumnHead {
                        Layout.preferredWidth: Style.orderColumnW + stackList.leftMargin
                        label: qsTr("Order")
                    }
                    TableGroove {}
                    TableColumnHead {
                        Layout.preferredWidth: panel.iconW
                        label: qsTr("Icon")
                    }
                    TableGroove {}
                    TableColumnHead {
                        Layout.fillWidth: true
                        label: qsTr("Name")
                    }
                    TableGroove {}
                    TableColumnHead {
                        Layout.preferredWidth: panel.receivesW
                        label: qsTr("Default Reception")
                    }
                    TableGroove {}
                    TableColumnHead {
                        Layout.preferredWidth: panel.previewW + 6
                        label: qsTr("P/P")
                    }
                }

                ListView {
                    id: stackList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The bar a gap in from the well's edge, as the Library's is.
                    Layout.rightMargin: Style.scrollGap
                    topMargin: 6
                    bottomMargin: 6
                    leftMargin: 6
                    rightMargin: Style.scrollGutter(stackList)
                    spacing: 3
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.dialog.stackRows
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: TableRow {
                        id: stack
                        required property int index
                        required property var modelData
                        readonly property string stackId: stack.modelData.id

                        implicitHeight: Style.groupRowH
                        selected: panel.dialog.shownStackId === stack.stackId
                        onClicked: panel.dialog.selectStack(stack.stackId)
                        onDoubleClicked: function (x) {
                            if (x >= nameCell.x && x < nameCell.x + nameCell.width) {
                                panel.dialog.selectStack(stack.stackId)
                                panel.editingId = stack.stackId
                            }
                        }

                        // Its place, front to back, a column of its own.
                        TableTextCell {
                            Layout.preferredWidth: Style.orderColumnW
                            text: String(stack.index + 1)
                            ink: Style.textSoft
                            centered: true
                        }
                        TableGroove {}
                        // Its icon.
                        Item {
                            Layout.preferredWidth: panel.iconW
                            Layout.fillHeight: true
                            IconPickerButton {
                                anchors.centerIn: parent
                                icons: panel.session ? panel.session.stacks.iconIds() : []
                                current: stack.modelData.icon
                                onPicked: function (id) {
                                    panel.dialog.selectStack(stack.stackId)
                                    panel.dialog.setStackIcon(stack.stackId, id)
                                }
                            }
                        }
                        TableGroove {}
                        TableTextCell {
                            id: nameCell
                            Layout.fillWidth: true
                            text: stack.modelData.name || stack.stackId
                            open: panel.editingId === stack.stackId
                            ink: Style.textBright
                            bold: true
                            onCommitted: function (text) { panel.endEdit(true, text) }
                            onCanceled: panel.endEdit(false, "")
                        }
                        TableGroove {}
                        Item {
                            Layout.preferredWidth: panel.receivesW
                            Layout.fillHeight: true
                            ChecklistField {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 6
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                manyText: qsTr("%1 item types")
                                noneText: qsTr("Nothing")
                                model: panel.dialog.receivesModel(stack.stackId).map(function (item) {
                                    item.icon = Icons.url(item.glyph, Style.iconIdle, 2)
                                    return item
                                })
                                checkedIds: panel.dialog.receivedKeys(stack.stackId)
                                onToggled: function (key) {
                                    panel.dialog.selectStack(stack.stackId)
                                    panel.dialog.toggleReceives(stack.stackId, key)
                                }
                            }
                        }
                        TableGroove {}
                        Item {
                            Layout.preferredWidth: panel.previewW
                            Layout.fillHeight: true
                            ToggleSwitch {
                                objectName: "stackPreview"
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                checked: !!stack.modelData.preview
                                onToggled: {
                                    panel.dialog.selectStack(stack.stackId)
                                    panel.dialog.setPreview(stack.stackId, !stack.modelData.preview)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
