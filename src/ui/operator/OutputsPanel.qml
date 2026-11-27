pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect
import "../js/Reorder.js" as Reorder

// The Outputs panel: a card per output switched on, with its icon, its live
// preview and its Freeze, Blank and settings buttons; drag a card's title to
// reorder. Outputs switched off (Settings > Outputs) have no card. A card's
// number, then B or F, blanks or freezes that output alone (Main.qml).
// The panel's frame and title are DockPanel's; the Output window is the
// window's, opened on ``outputSettingsRequested``.
Rectangle {
    id: root
    color: Style.panelBg

    required property SessionFacade session
    /// The card whose number was just pressed, waiting for B or F; -1 for none.
    property int armedIndex: -1
    /// A press in the panel: the window ends a search being typed elsewhere.
    signal interacted()
    /// Output ``name``'s gear: open its settings.
    signal outputSettingsRequested(string name)

    function outputNameAt(i) {
        const m = root.session ? root.session.outputsOn : null
        if (!m || i < 0 || i >= outputList.count)
            return ""
        return m.data(m.index(i, 0), OutputListModel.NameRole) || ""
    }

    function finishOutputDrag() {
        const from = outputList.dragFromIndex
        let insertBefore = outputList.dropInsertIndex
        outputList.dragFromIndex = -1
        outputList.dropInsertIndex = -1
        outputList.hideDragGhost()
        const to = Reorder.landing(from, insertBefore, outputList.count)
        if (!root.session || to < 0)
            return
        const shown = []
        for (let i = 0; i < outputList.count; ++i)
            shown.push(root.outputNameAt(i))
        const moved = Reorder.moved(shown, from, to)
        // The cards are the outputs on; those off keep their places among them.
        const all = root.session.outputs
        const names = []
        for (let i = 0, next = 0; i < all.rowCount(); ++i) {
            const name = all.data(all.index(i, 0), OutputListModel.NameRole)
            names.push(shown.indexOf(name) >= 0 ? moved[next++] : name)
        }
        root.session.outputSettings.reorderOutputs(names)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 8
        // Flush to the panel's foot so a grey pad cannot cover the last
        // preview as the list scrolls.
        anchors.bottomMargin: 0
        spacing: 8

        ListView {
            id: outputList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.session ? root.session.outputsOn : null
            spacing: 8
            interactive: !outputList.dragActive
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar {}

            property int dragFromIndex: -1
            property int dropInsertIndex: -1
            property bool dragActive: false
            property string dragName: ""
            property real dragGrabX: 16
            property real dragGrabY: 14

            function showDragGhost(name, grabX, grabY) {
                outputList.dragActive = true
                outputList.dragName = name
                outputList.dragGrabX = grabX
                outputList.dragGrabY = grabY
            }

            function moveDragGhostFrom(item, localX, localY) {
                const p = item.mapToItem(root, localX, localY)
                outputGhost.x = p.x - outputList.dragGrabX
                outputGhost.y = p.y - outputList.dragGrabY
            }

            function hideDragGhost() {
                outputList.dragActive = false
                outputList.dragName = ""
            }

            delegate: Item {
                id: cardShell
                required property string name
                required property string outputId
                required property string icon
                required property bool isBlank
                required property bool isFrozen
                required property bool isOpen
                required property real aspect
                required property int index
                /// Its number (1 to 9) was just pressed: the next B or F is this output's.
                readonly property bool armed: root.armedIndex === cardShell.index

                width: outputList.width - Style.scrollGutter(outputList)
                // Topbar + preview - chrome stays outside the well.
                readonly property int topbarH: 28
                // Live uses a 2px ring; idle uses 1px. Reserve that on every
                // side so the well cannot paint over the bottom edge.
                readonly property int frameW: cardShell.isOpen ? 2 : 1
                // The preview has the output's shape, the well's width wide
                // and no taller than that: a portrait output's stands in the
                // middle of a square well. AppScrollBar keeps the gutter
                // sticky so the height following the width cannot toggle
                // overflow on/off.
                readonly property real previewAspect: cardShell.aspect > 0 ? cardShell.aspect : 16 / 9
                // The well is inside the frame.
                readonly property int wellW: width - 2 * cardShell.frameW
                readonly property int previewH:
                    Math.max(72, Math.round(Math.min(cardShell.wellW,
                                                     cardShell.wellW / cardShell.previewAspect)))
                readonly property int previewW:
                    Math.round(Math.min(cardShell.wellW, cardShell.previewH * cardShell.previewAspect))
                // 26px title band + 1px hairline + well + frame on top/bottom.
                readonly property int cardH: (topbarH - 2) + 1 + previewH + 2 * frameW
                height: cardH
                readonly property bool isDragging: outputList.dragFromIndex === cardShell.index
                                                  && outputList.dragActive
                opacity: cardShell.isDragging ? 0.4 : 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: -2
                    height: 3
                    radius: 1
                    color: Style.dropIndicator
                    z: 30
                    visible: outputList.dragActive
                             && outputList.dropInsertIndex === cardShell.index
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 1
                    height: 3
                    radius: 1
                    color: Style.dropIndicator
                    z: 30
                    visible: outputList.dragActive
                             && cardShell.index === outputList.count - 1
                             && outputList.dropInsertIndex === outputList.count
                }

                // Soft shadow (web box-shadow stand-in - no effects module).
                Rectangle {
                    anchors.left: card.left
                    anchors.right: card.right
                    anchors.top: card.top
                    anchors.topMargin: 3
                    height: card.height
                    radius: Style.radius
                    color: Style.shadow
                    opacity: 0.45
                    z: 0
                }

                Rectangle {
                    id: card
                    width: parent.width
                    height: cardShell.cardH
                    color: Style.cardBg
                    border.color: cardShell.isOpen ? Style.liveGreen : Style.cardEdge
                    border.width: cardShell.frameW
                    radius: Style.radius
                    opacity: cardShell.isBlank ? 0.7 : 1.0
                    clip: true
                    z: 1

                    Column {
                    anchors.fill: parent
                    anchors.margins: cardShell.frameW

                    // ── Topbar: title + square controls ─────────────────────
                    Rectangle {
                        width: parent.width
                        // Even height so 22px buttons sit in 2px padding (hairline is
                        // a sibling below, not overlaid on this band).
                        height: cardShell.topbarH - 2
                        color: Style.cardTopbar

                        // The output's icon (Output window > General). It is
                        // also the badge that lights while the card's number
                        // (1 to 9) waits for a B or F.
                        Rectangle {
                            id: iconBadge
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            radius: Style.radiusSmall
                            color: cardShell.armed ? Style.accent : "transparent"
                            border.width: 1
                            border.color: cardShell.armed ? Style.accent : Style.cardEdge

                            Image {
                                anchors.centerIn: parent
                                width: 12
                                height: 12
                                sourceSize: Qt.size(12, 12)
                                source: Icons.url(cardShell.icon,
                                                  cardShell.armed ? Style.textOnAccent : Style.cardLabel,
                                                  2)
                            }
                        }

                        Text {
                            anchors.left: iconBadge.right
                            anchors.right: ctrlRow.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.leftMargin: 6
                            anchors.rightMargin: 4
                            verticalAlignment: Text.AlignVCenter
                            text: cardShell.name
                            color: Style.cardLabel
                            font.pixelSize: Style.fontSmall
                            font.bold: true
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        MouseArea {
                            id: titleDrag
                            anchors.left: parent.left
                            anchors.right: ctrlRow.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            hoverEnabled: true
                            cursorShape: titleDrag.dragging ? Qt.ClosedHandCursor
                                         : (containsMouse ? Qt.OpenHandCursor : Qt.ArrowCursor)
                            preventStealing: true
                            property bool dragging: false
                            property real pressContentY: 0
                            property real pressLocalX: 0
                            property real pressLocalY: 0

                            onPressed: function (mouse) {
                                root.interacted()
                                titleDrag.dragging = false
                                titleDrag.pressLocalX = mouse.x
                                titleDrag.pressLocalY = mouse.y
                                titleDrag.pressContentY =
                                        titleDrag.mapToItem(outputList.contentItem, mouse.x, mouse.y).y
                                outputList.dragFromIndex = cardShell.index
                                outputList.dropInsertIndex = cardShell.index
                            }
                            onPositionChanged: function (mouse) {
                                if (!pressed)
                                    return
                                const cy = titleDrag.mapToItem(outputList.contentItem, mouse.x, mouse.y).y
                                if (!titleDrag.dragging
                                        && Math.abs(cy - titleDrag.pressContentY) > 6) {
                                    titleDrag.dragging = true
                                    outputList.showDragGhost(
                                                cardShell.name,
                                                titleDrag.pressLocalX,
                                                titleDrag.pressLocalY)
                                    outputList.moveDragGhostFrom(titleDrag, mouse.x, mouse.y)
                                }
                                if (titleDrag.dragging) {
                                    outputList.dropInsertIndex = Reorder.slotInView(outputList, cy)
                                    outputList.moveDragGhostFrom(titleDrag, mouse.x, mouse.y)
                                    const viewY = titleDrag.mapToItem(outputList, mouse.x, mouse.y).y
                                    MultiSelect.autoScrollView(outputList, viewY, 28, 14)
                                }
                            }
                            onReleased: function (mouse) {
                                if (titleDrag.dragging)
                                    root.finishOutputDrag()
                                else {
                                    outputList.dragFromIndex = -1
                                    outputList.dropInsertIndex = -1
                                    outputList.hideDragGhost()
                                }
                                titleDrag.dragging = false
                            }
                            onCanceled: {
                                outputList.dragFromIndex = -1
                                outputList.dropInsertIndex = -1
                                outputList.hideDragGhost()
                                titleDrag.dragging = false
                            }
                        }

                        Row {
                            id: ctrlRow
                            anchors.right: parent.right
                            // Match the 2px padding above/below the 22px buttons.
                            anchors.rightMargin: 2
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            z: 2

                            PreviewCtrlBtn {
                                icon: "freeze"
                                active: cardShell.isFrozen
                                onClicked: root.session.live.toggleOutputFreeze(cardShell.outputId)
                            }
                            PreviewCtrlBtn {
                                icon: "blank"
                                active: cardShell.isBlank
                                onClicked: root.session.live.toggleOutputBlank(cardShell.outputId)
                            }
                            PreviewCtrlBtn {
                                icon: "settings"
                                active: false
                                onClicked: root.outputSettingsRequested(cardShell.name)
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Style.cardHairline
                    }

                    // ── Preview well ────────────────────────────────────────
                    Item {
                        width: parent.width
                        height: cardShell.previewH

                        Rectangle {
                            anchors.fill: parent
                            color: Style.previewWell
                        }

                        OutputPreviewItem {
                            anchors.centerIn: parent
                            width: cardShell.previewW
                            height: cardShell.previewH
                            outputId: cardShell.outputId
                            session: root.session
                        }
                    }
                    }
                } // card

                // Sibling of `card` so clip:true cannot nibble the bottom edge.
                // (A Rectangle border is also painted under its children, so the
                // preview well would hide the live ring along the bottom.)
                Rectangle {
                    anchors.fill: card
                    radius: Style.radius
                    color: "transparent"
                    border.color: Style.liveGreen
                    border.width: 2
                    z: 2
                    visible: cardShell.isOpen
                    opacity: card.opacity
                }
                Rectangle {
                    anchors.fill: card
                    anchors.margins: cardShell.frameW
                    radius: Math.max(0, Style.radius - 1)
                    color: "transparent"
                    border.color: Style.openOutline
                    border.width: 2
                    z: 3
                    visible: cardShell.isFrozen
                    opacity: card.opacity
                }
            } // cardShell

            Column {
                anchors.centerIn: parent
                width: Math.min(parent.width - 24, 260)
                visible: outputList.count === 0
                spacing: 4
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: qsTr("No outputs")
                    color: Style.textMuted
                    font.pixelSize: Style.fontUi
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Nothing is shown until there is one. Add one in Settings › Outputs.")
                    color: Style.textCaption
                    font.pixelSize: Style.fontSmall
                }
            }
        }
    }

    // Shared square control - web Feather SVGs, tinted by active state.
    component PreviewCtrlBtn: AppButton {
        property string icon: ""
        property bool active: false
        size: "compact"
        implicitWidth: 24
        implicitHeight: 22
        width: 24
        height: 22
        toggled: active
        toggledKind: icon === "blank" || icon === "freeze" ? icon : ""
        iconSource: Icons.url(icon, ink)
        onPressed: root.interacted()
    }

    // Floating drag ghost for output card reorder (title-bar grab).
    Rectangle {
        id: outputGhost
        visible: outputList.dragActive
        z: 2000
        width: Math.min(outputList.width, 220)
        height: 28
        radius: Style.radius
        opacity: 0.92
        color: Style.cardTopbar
        border.width: 1
        border.color: Style.accent

        Text {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            verticalAlignment: Text.AlignVCenter
            text: outputList.dragName
            color: Style.cardLabel
            font.pixelSize: Style.fontCaption
            font.bold: true
            font.letterSpacing: 0.3
            elide: Text.ElideRight
        }
    }

    Component.onCompleted: {
        if (root.session)
            root.session.destinations.refreshDisplays()
    }
}
