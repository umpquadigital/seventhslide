pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// The song editor's title and play order, as one form group: the title, then
// the arrangement, the order the verses are dragged into (codes in the XML
// view) and + for a title or theme change.
GroupList {
    id: group
    /// The song being edited.
    required property SongEditState dialog
    /// The lyrics panel, for the verse editor's and the drags' helpers.
    /// Untyped, since SongLyricsPanel makes this type and Qt refuses the
    /// cycle; tools/panel_refs.py checks the names used on it.
    required property var panel // SongLyricsPanel
    // What the panel's focus and drag helpers reach.
    readonly property alias titleBox: titleField
    readonly property alias strip: orderStrip
    readonly property alias stripFlick: orderFlick
    readonly property alias chips: chipFlow

    /// The labels' column, so the fields line up.
    readonly property int labelW: 52

    GroupRow {
        Text {
            Layout.preferredWidth: group.labelW
            text: qsTr("Title")
            color: Style.text
            font.pixelSize: Style.fontBody
        }
        DialogField {
            id: titleField
            Layout.fillWidth: true
            escapeClearsFocus: true
            text: group.dialog.titleText
            placeholder: qsTr("Song title")
            onEdited: function (v) { group.dialog.titleText = v }
        }
    }

    GroupRow {
        Text {
            Layout.preferredWidth: group.labelW
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 5
            text: qsTr("Order")
            color: Style.text
            font.pixelSize: Style.fontBody
        }

        // The arrangement: which order this is, and the menu to make others.
        DropdownField {
            id: arrField
            Layout.preferredWidth: 124
            Layout.preferredHeight: Style.controlH
            Layout.alignment: Qt.AlignTop
            text: group.dialog.activeArrangementName
            open: arrMenu.visible
            onClicked: {
                group.panel.clearFieldFocus()
                arrMenu.visible ? arrMenu.close() : arrMenu.openAt()
            }

            AppMenu {
                id: arrMenu
                objectName: "arrangementMenu"
                anchorItem: arrField
                MenuHeading {
                    text: qsTr("Arrangements")
                }
                Repeater {
                    model: group.dialog.arrangements
                    delegate: MenuItemRow {
                        required property var modelData
                        checkable: true
                        checked: String(modelData.id) === group.dialog.activeArrangementId
                        label: group.dialog.arrangementLabel(modelData.name)
                        onTriggered: {
                            arrMenu.close()
                            group.dialog.selectArrangement(modelData.id)
                        }
                    }
                }
                MenuRule {}
                MenuItemRow {
                    icon: "plus"
                    label: qsTr("New Arrangement…")
                    onTriggered: {
                        arrMenu.close()
                        group.dialog.beginNewArrangement()
                    }
                }
                MenuItemRow {
                    icon: "edit"
                    label: qsTr("Rename…")
                    onTriggered: {
                        arrMenu.close()
                        group.dialog.beginRenameArrangement()
                    }
                }
                MenuItemRow {
                    icon: "copy"
                    label: qsTr("Duplicate")
                    onTriggered: {
                        arrMenu.close()
                        group.dialog.duplicateArrangement()
                    }
                }
                MenuItemRow {
                    icon: "trash"
                    label: qsTr("Delete")
                    danger: true
                    enabled: group.dialog.arrangements.length > 1
                    detail: enabled ? "" : qsTr("A song keeps at least one arrangement.")
                    onTriggered: {
                        arrMenu.close()
                        group.dialog.deleteArrangement()
                    }
                }
            }
        }

        // The order, as chips that drag; a verse dragged here joins it.
        Rectangle {
            id: orderStrip
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            implicitHeight: Style.controlH + orderHBar.reservedHeight
            Layout.preferredHeight: implicitHeight
            visible: group.dialog.lyricsMode === "guided"
            radius: Style.radiusSmall
            color: group.dialog.verseDragActive && group.dialog.orderDropHover
                   ? Style.dropTint : Style.fieldFace
            border.color: group.dialog.verseDragActive && group.dialog.orderDropHover
                          ? Style.dropIndicator
                          : ((group.dialog.verseDragActive || group.dialog.chipDragActive)
                             ? Style.controlEdgeHover : Style.controlEdge)
            clip: true

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 9
                anchors.rightMargin: 9
                visible: group.dialog.orderChips.length === 0
                text: group.dialog.verseDragActive
                      ? qsTr("Drop to add to the order")
                      : qsTr("Drag verses here, or use Order")
                color: group.dialog.verseDragActive && group.dialog.orderDropHover
                       ? Style.dropInk : Style.fieldPlaceholder
                font.pixelSize: Style.fontBody
                elide: Text.ElideRight
            }

            Flickable {
                id: orderFlick
                anchors.fill: parent
                anchors.margins: 1
                contentWidth: Math.max(width, chipFlow.x + chipFlow.implicitWidth + 3)
                contentHeight: height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                interactive: !group.dialog.chipDragActive && !group.dialog.verseDragActive
                ScrollBar.horizontal: AppScrollBar {
                    id: orderHBar
                    contentGap: 0
                }

                Row {
                    id: chipFlow
                    x: 2
                    y: Math.max(0, Math.round((orderFlick.height - orderHBar.reservedHeight
                                               - height) / 2))
                    spacing: 3

                    Repeater {
                        model: group.dialog.orderChips
                        delegate: Rectangle {
                            id: chip
                            required property int index
                            required property string modelData
                            readonly property bool titleChip: SongText.isTitleCode(modelData)
                            readonly property bool themeChip: SongText.isThemeCode(modelData)
                            readonly property bool selected:
                                group.dialog.selectedOrderIndex === index
                            readonly property bool isDragging:
                                group.dialog.chipDragActive && group.dialog.chipDragFrom === index
                            readonly property bool dropBefore:
                                group.dialog.chipDragActive && group.dialog.chipDropHover === index
                                && group.dialog.chipDropBefore
                            readonly property bool dropAfter:
                                group.dialog.chipDragActive && group.dialog.chipDropHover === index
                                && !group.dialog.chipDropBefore

                            height: 20
                            width: chipRow.implicitWidth + 10
                            radius: Style.radiusSmall
                            opacity: chip.isDragging ? 0.25 : 1
                            color: chip.themeChip ? Style.chipThemeBg
                                 : (chip.titleChip ? Style.chipTitleBg : Style.chipVerseBg)
                            border.color: chip.selected ? Style.focusEdge
                                        : (chip.themeChip ? Style.chipThemeEdge
                                        : (chip.titleChip ? Style.chipTitleEdge : Style.chipVerseEdge))

                            Row {
                                id: chipRow
                                anchors.centerIn: parent
                                // The chip's height, so the label and the x
                                // centre on the chip's middle, not the label's.
                                height: parent.height
                                spacing: 3
                                BadgeText {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: group.dialog.chipLabel(chip.modelData)
                                    color: chip.themeChip ? Style.chipThemeInk
                                         : (chip.titleChip ? Style.chipTitleInk : Style.chipVerseInk)
                                    font.pixelSize: Style.fontSmall
                                    font.bold: true
                                }
                                Image {
                                    id: chipX
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 10
                                    height: 10
                                    source: Icons.url("close", chip.themeChip ? Style.chipThemeInkFaint
                                                      : (chip.titleChip ? Style.chipTitleInkFaint
                                                                        : Style.chipVerseInkFaint), 3)
                                    sourceSize: Qt.size(10, 10)
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -4
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: group.dialog.removeOrderChip(chip.index)
                                    }
                                }
                            }

                            MouseArea {
                                id: chipDragArea
                                anchors.fill: parent
                                anchors.rightMargin: 13
                                z: 2
                                hoverEnabled: true
                                preventStealing: true
                                cursorShape: pressed || chip.isDragging ? Qt.ClosedHandCursor
                                                                        : Qt.OpenHandCursor

                                property real pressX: 0
                                property real pressY: 0
                                property bool dragging: false

                                onPressed: function (mouse) {
                                    chipDragArea.pressX = mouse.x
                                    chipDragArea.pressY = mouse.y
                                    chipDragArea.dragging = false
                                }
                                onPositionChanged: function (mouse) {
                                    if (!pressed)
                                        return
                                    const dx = mouse.x - chipDragArea.pressX
                                    const dy = mouse.y - chipDragArea.pressY
                                    if (!chipDragArea.dragging && (dx * dx + dy * dy) > 36) {
                                        chipDragArea.dragging = true
                                        group.dialog.beginChipOrderDrag(chip.index)
                                        group.panel.clearFieldFocus()
                                        group.panel.updateChipOrderDrag(chip, mouse.x, mouse.y)
                                    } else if (chipDragArea.dragging) {
                                        group.panel.updateChipOrderDrag(chip, mouse.x, mouse.y)
                                    }
                                }
                                onReleased: function (mouse) {
                                    if (chipDragArea.dragging) {
                                        group.panel.updateChipOrderDrag(chip, mouse.x, mouse.y)
                                        group.dialog.finishChipOrderDrag()
                                    } else {
                                        group.dialog.selectOrderChip(chip.index)
                                    }
                                    chipDragArea.dragging = false
                                }
                                onCanceled: {
                                    group.dialog.cancelChipOrderDrag()
                                    chipDragArea.dragging = false
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 2
                                radius: 1
                                color: Style.dropIndicator
                                visible: chip.dropBefore
                                z: 3
                            }
                            Rectangle {
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 2
                                radius: 1
                                color: Style.dropIndicator
                                visible: chip.dropAfter
                                z: 3
                            }
                        }
                    }
                }
            }
        }

        DialogField {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            visible: group.dialog.lyricsMode === "raw"
            escapeClearsFocus: true
            text: group.dialog.rawOrder
            placeholder: qsTr("TH1 T1 V1 C1 V2 C1")
            onEdited: function (v) { group.dialog.rawOrder = v }
        }

        // A title or theme change, added at the end of the order.
        AppButton {
            id: addChipBtn
            Layout.alignment: Qt.AlignTop
            visible: group.dialog.lyricsMode === "guided"
            size: "control"
            iconSource: Icons.url("plus", ink)
            onClicked: {
                group.panel.clearFieldFocus()
                addMenu.visible ? addMenu.close() : addMenu.openAt()
            }

            AppMenu {
                id: addMenu
                objectName: "addChipMenu"
                anchorItem: addChipBtn
                alignRight: true
                minWidth: 160
                MenuItemRow {
                    icon: "titles"
                    label: qsTr("Title")
                    onTriggered: {
                        addMenu.close()
                        group.dialog.addTitleChip()
                    }
                }
                MenuItemRow {
                    icon: "image"
                    label: qsTr("Theme Change")
                    onTriggered: {
                        addMenu.close()
                        group.dialog.addThemeChip()
                    }
                }
            }
        }
    }
}
