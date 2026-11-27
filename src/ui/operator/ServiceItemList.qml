pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect
import "../js/Reorder.js" as Reorder

// The open service's items: one row each, selected, reordered by drag, and
// taking drops from the library. The row live on the focused stack is blue;
// one cued in its Preview is not marked here (the Preview's well shows it).
// A double-click sends a row to its stack (cued, where the stack has a
// Preview); Shift+double-click sends it straight live.
ListView {
        id: itemList
    /// Untyped, since ServicePanel makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var panel // ServicePanel
    /// What follows the pointer while a row is dragged.
    required property Item ghost
        anchors.fill: parent
        anchors.margins: 6
        clip: true
        model: itemList.panel.session ? itemList.panel.session.serviceItems : null
        spacing: 3
        boundsBehavior: Flickable.StopAtBounds
        interactive: !itemList.panel.itemSelection.marqueeActive && !itemList.dragActive
        ScrollBar.vertical: AppScrollBar {}

        // Empty-list / container cue while dragging from the library.
        Rectangle {
            anchors.fill: parent
            z: -1
            radius: Style.radiusSmall
            color: "transparent"
            border.width: itemList.panel.libAddDragActive
                          && itemList.dropInsertIndex >= 0 ? 1 : 0
            border.color: Style.accent
            opacity: 0.7
        }

        property int dragFromIndex: -1
        property int dropInsertIndex: -1
        property bool dragActive: false
        property string dragTitle: ""
        property string dragIconKind: ""
        property int dragDisplayNumber: 0
        property bool dragIsDivider: false
        property bool dragIsLive: false
        property bool dragHasOverrides: false
        property real dragGrabX: 16
        property real dragGrabY: 16

        property int dragMoveCount: 1

        function showDragGhost(rowItem, grabX, grabY) {
            itemList.dragActive = true
            const n = (itemList.panel.selectedItemIds.length > 1
                       && itemList.panel.itemSelection.contains(rowItem.itemId))
                    ? itemList.panel.selectedItemIds.length : 1
            itemList.dragMoveCount = n
            itemList.dragTitle = n > 1 ? qsTr("%1 items").arg(n) : rowItem.title
            itemList.dragIconKind = n > 1 ? "" : rowItem.iconKind
            itemList.dragDisplayNumber = n > 1 ? 0 : rowItem.displayNumber
            itemList.dragIsDivider = n > 1 ? false : rowItem.isDivider
            itemList.dragIsLive = rowItem.selected
            itemList.dragHasOverrides = n > 1 ? false : rowItem.hasOverrides
            itemList.dragGrabX = grabX
            itemList.dragGrabY = grabY
        }

        function moveDragGhostFrom(item, localX, localY) {
            const p = item.mapToItem(itemList.ghost.parent, localX, localY)
            itemList.ghost.x = p.x - itemList.dragGrabX
            itemList.ghost.y = p.y - itemList.dragGrabY
        }

        function hideDragGhost() {
            itemList.dragActive = false
            itemList.dragMoveCount = 1
        }

        delegate: Rectangle {
            id: row
            required property string title
            required property string itemType
            required property int itemIndex
            required property var itemId
            required property bool selected
            required property bool hasOverrides
            required property int displayNumber
            required property var folderImages
            required property string mediaFile
            required property int mediaKind
            required property string stackId
            required property string stackIcon
            required property var alsoPlayIds
            required property string alsoPlayWith
            required property int linkSet

            width: ListView.view.width - Style.scrollGutter(ListView.view)
            readonly property bool isDivider: row.itemType === "divider"
            readonly property bool isImageFolder: row.itemType === "image_folder"
            readonly property var images: {
                if (!row.isImageFolder || !row.folderImages)
                    return []
                return row.folderImages
            }
            readonly property bool folderOpen: row.isImageFolder
                    && itemList.panel.isFolderExpanded(row.itemId)
            readonly property bool isDragging: itemList.dragActive
                    && (itemList.dragFromIndex === row.itemIndex
                        || (itemList.dragMoveCount > 1
                            && itemList.panel.itemSelection.contains(row.itemId)))
                                               && itemList.dragActive
            readonly property bool uiSelected: itemList.panel.itemSelection.contains(row.itemId)
            readonly property bool isLinked: !!(row.alsoPlayIds && row.alsoPlayIds.length)
                    || !!(row.alsoPlayWith && row.alsoPlayWith.length)
            readonly property bool hasStackPin: !!(row.stackId && row.stackId.length)
            readonly property string iconKind: itemList.panel.typeIconKind(row.itemType)
            readonly property int headerH: row.isDivider ? 26 : Style.listRowH
            readonly property int nestedH: row.folderOpen
                    ? Math.max(0, row.images.length) * 28 : 0
            height: row.headerH + row.nestedH
            radius: Style.radiusSmall
            opacity: row.isDragging ? 0.4 : 1
            color: {
                if (row.isDivider)
                    return Style.dividerRow
                if (row.selected)
                    return rowDrag.containsMouse ? Style.liveRowHover : Style.liveRow
                return Style.panelRowBg(row.uiSelected, rowDrag.containsMouse)
            }
            clip: true

            // Drop insert indicators.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 2
                z: 3
                visible: (itemList.dragActive || itemList.panel.libAddDragActive)
                         && itemList.dropInsertIndex === row.itemIndex
                color: Style.accent
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 2
                z: 3
                visible: (itemList.dragActive || itemList.panel.libAddDragActive)
                         && itemList.dropInsertIndex === row.itemIndex + 1
                         && row.itemIndex === itemList.count - 1
                color: Style.accent
            }

            // Left accent bar.
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                height: row.headerH
                width: 3
                radius: 1
                visible: !row.isDivider
                color: {
                    if (row.selected)
                        return Style.accent
                    return Style.listAccent(row.uiSelected, rowDrag.containsMouse)
                }
            }

            // Divider chrome
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                visible: row.isDivider
                color: Style.rule
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                y: row.headerH - 1
                height: 1
                visible: row.isDivider
                color: Style.rule
            }

            Item {
                id: headerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: row.headerH

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: row.isDivider ? 8 : 10
                    anchors.rightMargin: 10
                    spacing: 6

                    Image {
                        visible: row.isImageFolder
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                        Layout.rightMargin: 2
                        source: Icons.url(row.folderOpen ? "chevronDown" : "chevronRight",
                                          row.selected ? Style.textStrong : Style.textCaption, 2.5)
                        sourceSize: Qt.size(10, 10)
                    }

                    Text {
                        visible: !row.isDivider && row.displayNumber > 0
                        text: row.displayNumber + "."
                        color: row.selected ? Style.textStrong : Style.textMuted
                        font.pixelSize: Style.fontBody
                        Layout.preferredWidth: implicitWidth
                    }

                    Image {
                        visible: !row.isDivider && row.iconKind.length > 0
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        source: itemList.panel.iconUrl(row.iconKind,
                                             row.selected ? Style.textStrong : Style.iconIdle)
                        sourceSize: Qt.size(14, 14)
                        smooth: true
                        asynchronous: false
                        cache: true
                    }

                    SectionTitle {
                        visible: row.isDivider
                        Layout.fillWidth: true
                        title: row.title
                    }
                    Text {
                        visible: !row.isDivider
                        Layout.fillWidth: true
                        text: row.title
                        color: row.selected ? Style.textStrong : Style.text
                        font.pixelSize: Style.fontBody
                        elide: Text.ElideRight
                    }

                    // A file not here yet says where it is.
                    ArrivalMark {
                        Layout.maximumWidth: row.width / 2
                        maxWidth: row.width / 2
                        arrival: itemList.panel.session && row.mediaKind >= 0
                                 ? itemList.panel.session.media.arrival(row.mediaKind, row.mediaFile,
                                                                        itemList.panel.session.media.arrivalsRevision)
                                 : ({})
                    }

                    Text {
                        visible: row.isImageFolder && row.images.length > 0
                        text: String(row.images.length)
                        color: row.selected ? Style.countInkSel : Style.textMuted
                        font.pixelSize: Style.fontSmall
                    }

                    Text {
                        visible: row.hasOverrides && !row.isDivider
                        text: "●"
                        color: Style.overrideDot
                        font.pixelSize: Style.fontMicro
                    }

                    Image {
                        visible: row.isLinked
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        source: itemList.panel.iconUrl("chain", row.linkSet >= 0
                                             ? Style.linkSetColor(row.linkSet)
                                             : (row.selected ? Style.textStrong : Style.textHover))
                        sourceSize: Qt.size(14, 14)
                        smooth: true
                        asynchronous: false
                        cache: true
                    }

                    Image {
                        visible: row.hasStackPin
                        Layout.preferredWidth: 14
                        Layout.preferredHeight: 14
                        source: Icons.url(
                                      row.stackIcon,
                                      row.selected ? Style.textStrong : Style.textHover, 2)
                        sourceSize: Qt.size(14, 14)
                        smooth: true
                        asynchronous: false
                        cache: true
                    }
                }

                MouseArea {
                    id: rowDrag
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: row.isDivider ? Qt.ArrowCursor : Qt.PointingHandCursor
                    preventStealing: true
                    property bool dragging: false
                    property real pressContentY: 0
                    property real pressLocalX: 0
                    property real pressLocalY: 0

                    onPressed: function(mouse) {
                        itemList.panel.interacted()
                        rowDrag.dragging = false
                        rowDrag.pressLocalX = mouse.x
                        rowDrag.pressLocalY = mouse.y
                        rowDrag.pressContentY =
                                rowDrag.mapToItem(itemList.contentItem, mouse.x, mouse.y).y
                        itemList.panel.itemPressX = mouse.x
                        itemList.panel.itemPressY = mouse.y
                        itemList.panel.itemSelection.press(row.itemId, mouse.modifiers)
                        // Chevron hit - expand/collapse instead of drag.
                        if (row.isImageFolder && mouse.x < 28) {
                            itemList.panel.toggleFolderExpanded(row.itemId)
                            itemList.dragFromIndex = -1
                            return
                        }
                        itemList.dragFromIndex = row.itemIndex
                        itemList.dropInsertIndex = row.itemIndex
                    }
                    onPositionChanged: function(mouse) {
                        if (!pressed)
                            return
                        if (itemList.panel.itemSelection.marqueeActive) {
                            itemList.panel.itemSelection.updateMarquee(rowDrag, mouse.x, mouse.y)
                            return
                        }
                        const cy = rowDrag.mapToItem(itemList.contentItem, mouse.x, mouse.y).y
                        if (!rowDrag.dragging
                                && Math.abs(cy - rowDrag.pressContentY) > itemList.panel.itemDragThreshold) {
                            rowDrag.dragging = true
                            itemList.panel.itemSelection.keepOnRelease()
                            itemList.showDragGhost(row, rowDrag.pressLocalX, rowDrag.pressLocalY)
                            itemList.moveDragGhostFrom(rowDrag, mouse.x, mouse.y)
                        }
                        if (rowDrag.dragging) {
                            itemList.dropInsertIndex = Reorder.slotInView(itemList, cy)
                            itemList.moveDragGhostFrom(rowDrag, mouse.x, mouse.y)
                            const viewY = rowDrag.mapToItem(itemList, mouse.x, mouse.y).y
                            MultiSelect.autoScrollView(itemList, viewY, 28, 12)
                        }
                    }
                    onReleased: function(mouse) {
                        if (itemList.panel.itemSelection.marqueeActive) {
                            itemList.panel.itemSelection.updateMarquee(rowDrag, mouse.x, mouse.y)
                            itemList.panel.itemSelection.endMarquee()
                        } else if (rowDrag.dragging) {
                            itemList.panel.finishItemDrag()
                        } else {
                            itemList.dragFromIndex = -1
                            itemList.dropInsertIndex = -1
                            itemList.hideDragGhost()
                            itemList.panel.itemSelection.release()
                        }
                        rowDrag.dragging = false
                    }
                    onCanceled: {
                        if (itemList.panel.itemSelection.marqueeActive)
                            itemList.panel.itemSelection.endMarquee()
                        itemList.dragFromIndex = -1
                        itemList.dropInsertIndex = -1
                        itemList.hideDragGhost()
                        rowDrag.dragging = false
                        itemList.panel.itemSelection.keepOnRelease()
                    }
                    onDoubleClicked: function (mouse) {
                        itemList.panel.itemSelection.selectOnly(row.itemId)
                        itemList.panel.session.live.selectServiceItem(
                            row.itemIndex, !!(mouse.modifiers & Qt.ShiftModifier))
                    }
                }
            }

            Column {
                id: nestedImages
                visible: row.folderOpen
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.topMargin: row.headerH

                Repeater {
                    model: row.images
                    delegate: Rectangle {
                        id: nested
                        required property int index
                        required property string modelData
                        // Not parent.width: a row being torn down has no parent.
                        width: nestedImages.width
                        height: 28
                        color: {
                            const live = row.selected
                                    && itemList.panel.session
                                    && itemList.panel.session.program.imageMode
                                    && itemList.panel.session.program.lineCursor === nested.index
                            if (live)
                                return nestedHover.containsMouse ? Style.lineLiveHover : Style.lineLive
                            return nestedHover.containsMouse ? Style.chipBg : Style.thumbWell
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 28
                            anchors.rightMargin: 8
                            spacing: 6

                            Text {
                                text: (nested.index + 1) + "/" + row.images.length
                                color: Style.textMuted
                                font.pixelSize: Style.fontCaption
                                Layout.preferredWidth: 36
                            }

                            Image {
                                Layout.preferredWidth: 32
                                Layout.preferredHeight: 18
                                source: itemList.panel.session
                                        ? itemList.panel.session.media.mediaThumbUrl(MediaKind.Image, nested.modelData,
                                                                               itemList.panel.session.media.filesRevision)
                                        : ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                                sourceSize: Qt.size(64, 36)
                            }

                            Text {
                                Layout.fillWidth: true
                                text: itemList.panel.session
                                      ? (itemList.panel.session.media.mediaFileDisplayName(MediaKind.Image, nested.modelData)
                                         || nested.modelData)
                                      : nested.modelData
                                color: Style.text
                                font.pixelSize: Style.fontSmall
                                elide: Text.ElideRight
                            }

                            ArrivalMark {
                                Layout.maximumWidth: nestedImages.width / 2
                                maxWidth: nestedImages.width / 2
                                arrival: itemList.panel.session
                                         ? itemList.panel.session.media.arrival(MediaKind.Image, nested.modelData,
                                                                                itemList.panel.session.media.arrivalsRevision)
                                         : ({})
                            }
                        }

                        MouseArea {
                            id: nestedHover
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                itemList.panel.itemSelection.selectOnly(row.itemId)
                            }
                            onDoubleClicked: function (mouse) {
                                itemList.panel.itemSelection.selectOnly(row.itemId)
                                itemList.panel.session.live.selectServiceItemAtImage(
                                    row.itemIndex, nested.index,
                                    !!(mouse.modifiers & Qt.ShiftModifier))
                            }
                        }
                    }
                }
            }
        }

    MarqueeRect {
        view: itemList
        active: itemList.panel.itemSelection.marqueeActive
        contentX1: itemList.panel.itemSelection.marqueeX1
        contentY1: itemList.panel.itemSelection.marqueeY1
        contentX2: itemList.panel.itemSelection.marqueeX2
        contentY2: itemList.panel.itemSelection.marqueeY2
    }
}
