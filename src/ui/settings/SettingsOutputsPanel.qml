pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// Outputs page of Settings, laid out as Bibles is: a dark well holding a table
// of every output, Order, Icon, Name, Size and On, and over it arrows that
// move the one picked up and down the list, then Settings, Delete and New. A
// double click, or Settings, opens its Output Settings. An output switched
// off, on this computer, keeps everything it has and is left out of the show;
// its row fades. Adding, ordering, switching and deleting outputs are its state's
// (SettingsOutputsState); changing one is the Output window's.
Item {
    id: panel

    required property SettingsOutputsState dialog
    /// The columns but Order and Name, header and rows alike, padding and
    /// all: the icon with a little room either side, as Stacks' pickers.
    readonly property int iconW: Style.tileSize + 16
    readonly property int sizeW: 116
    readonly property int onW: 56

    /// Move the output picked ``step`` rows, keeping it in view.
    function moveShown(step) {
        panel.dialog.moveShown(step)
        outputList.positionViewAtIndex(panel.dialog.shownIndex, ListView.Contain)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Outputs")
            text: qsTr("Each output is its own screen: the main screen in the room, a monitor "
                       + "facing the platform, a livestream. Add one here, then open its settings "
                       + "to choose its size, themes and where it is sent. One switched off is left "
                       + "out of the show on this computer, and keeps all of it.")
        }

        // Up and Down for the one picked, then Settings, Delete and New, over
        // the well.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            AppButton {
                objectName: "outputMoveUp"
                size: "control"
                iconSource: Icons.url("up", ink)
                enabled: panel.dialog.canMoveShown(-1)
                onClicked: panel.moveShown(-1)
            }
            AppButton {
                objectName: "outputMoveDown"
                size: "control"
                iconSource: Icons.url("down", ink)
                enabled: panel.dialog.canMoveShown(1)
                onClicked: panel.moveShown(1)
            }
            Item { Layout.fillWidth: true }
            AppButton {
                size: "control"
                iconSource: Icons.url("edit", ink)
                enabled: panel.dialog.shownId.length > 0
                onClicked: panel.dialog.openShown()
            }
            AppButton {
                size: "control"
                iconSource: Icons.url("trash", ink)
                enabled: panel.dialog.shownId.length > 0
                onClicked: panel.dialog.beginDeleteOutput()
            }
            AppButton {
                size: "control"
                primary: true
                label: qsTr("New")
                iconSource: Icons.url("plus", ink)
                enabled: !!panel.dialog.session && panel.dialog.session.ready
                onClicked: panel.dialog.addOutput()
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
                    rightInset: Style.scrollGutter(outputList)

                    // The places' column; the rows' inset from the well's
                    // edge with it, so the grooves line up.
                    TableColumnHead {
                        Layout.preferredWidth: Style.orderColumnW + outputList.leftMargin
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
                        Layout.preferredWidth: panel.sizeW
                        label: qsTr("Size")
                    }
                    TableGroove {}
                    TableColumnHead {
                        Layout.preferredWidth: panel.onW + 6
                        label: qsTr("On")
                    }
                }

                ListView {
                    id: outputList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The bar a gap in from the well's edge, as the Library's is.
                    Layout.rightMargin: Style.scrollGap
                    topMargin: 6
                    bottomMargin: 6
                    leftMargin: 6
                    rightMargin: Style.scrollGutter(outputList)
                    spacing: 3
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.dialog.outputs
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: TableRow {
                        id: output
                        required property int index
                        required property string name
                        required property string outputId
                        required property string icon
                        required property bool isOn
                        required property int canvasWidth
                        required property int canvasHeight

                        implicitHeight: Style.groupRowH
                        selected: output.outputId === panel.dialog.shownId
                        onClicked: panel.dialog.selectedId = output.outputId
                        onDoubleClicked: {
                            panel.dialog.selectedId = output.outputId
                            panel.dialog.outputSettingsRequested(output.name)
                        }

                        // Its place, 1 the first.
                        TableTextCell {
                            Layout.preferredWidth: Style.orderColumnW
                            text: String(output.index + 1)
                            ink: Style.textSoft
                            centered: true
                            dim: !output.isOn
                        }
                        TableGroove {}
                        // Its glyph, a column of its own.
                        Item {
                            Layout.preferredWidth: panel.iconW
                            Layout.fillHeight: true
                            opacity: output.isOn ? 1 : Style.offOpacity
                            HandleTile {
                                anchors.centerIn: parent
                                iconSource: Icons.url(output.icon, Style.textSoft, 2)
                            }
                        }
                        TableGroove {}
                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            opacity: output.isOn ? 1 : Style.offOpacity
                            Text {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: output.name
                                color: Style.textBright
                                font.pixelSize: Style.fontBody
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                        }
                        TableGroove {}
                        TableTextCell {
                            Layout.preferredWidth: panel.sizeW
                            text: output.canvasWidth
                                  ? qsTr("%1 × %2").arg(output.canvasWidth).arg(output.canvasHeight)
                                  : ""
                            ink: Style.textSoft
                            dim: !output.isOn
                        }
                        TableGroove {}
                        Item {
                            Layout.preferredWidth: panel.onW
                            Layout.fillHeight: true
                            ToggleSwitch {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                checked: output.isOn
                                onToggled: panel.dialog.setOutputOn(output.outputId, !output.isOn)
                            }
                        }
                    }

                    // None: nothing is shown anywhere until there is one.
                    EmptyListNote {
                        visible: outputList.count === 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        title: qsTr("No outputs")
                        hint: qsTr("Nothing is shown until there is one. Use New to add an output.")
                    }
                }
            }
        }
    }
}
