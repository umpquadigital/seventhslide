pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide
import "../../js/MultiSelect.js" as MultiSelect

// The slide editor's sorter: the deck's slides (and cues, under line
// control) as thumbnails, picked, renamed and dragged into a new order. Shift
// picks a run of slides, and Ctrl+A every slide, for Copy. Its drag and row
// geometry are here, next to the list they measure. No header: the slide
// commands are on the ribbon.
Rectangle {
    id: sorterPane

    /// The editor's state.
    required property SlideEditState dialog
    /// ``dialog.session``, typed, so calls on it are checked.
    readonly property SessionFacade session: sorterPane.dialog.session

    property int sorterDragSlide: -1
    property bool sorterDragging: false
    property int sorterDropInsertBefore: -1
    property int sorterDropLastCue: -1
    property int sorterHoverIndex: -1

    /// Inset around the sorter ListView (LibraryPanel.listPad). Lives on the
    /// layout, not Flickable left/rightMargin, so the scrollbar sits in the
    /// usual content->gap->bar rhythm instead of a stacked dead column.
    readonly property int sorterListPad: 6

    function sorterRowHeight(row) {
        if (!row)
            return 28
        if (row.kind === "cue")
            return row.hasChords ? 42 : 28
        const w = Math.max(1, sorter.width - Style.scrollGutter(sorter))
        const scale = (sorterPane.session && sorterPane.dialog.edit.cues.lineControl) ? 0.55 : 1
        return Math.max(36, Math.round(w * sorterPane.dialog.canvasH / Math.max(1, sorterPane.dialog.canvasW) * scale))
    }

    function sorterRowTop(idx) {
        let y = sorter.topMargin
        if (!sorterPane.session || idx <= 0)
            return y
        const m = sorterPane.dialog.edit.slides.list
        for (let i = 0; i < idx && i < sorter.count; ++i)
            y += sorterPane.sorterRowHeight(m.rowAt(i)) + sorter.spacing
        return y
    }

    function sorterItemIndexAtY(contentY) {
        if (!sorterPane.session || sorter.count <= 0)
            return -1
        let y = sorter.topMargin
        const m = sorterPane.dialog.edit.slides.list
        for (let i = 0; i < sorter.count; ++i) {
            const h = sorterPane.sorterRowHeight(m.rowAt(i))
            if (contentY < y + h + sorter.spacing)
                return i
            y += h + sorter.spacing
        }
        return sorter.count - 1
    }

    /// The cue whose row ends nearest ``contentY``; -1 when there are none.
    function sorterLastCueAt(contentY) {
        if (!sorterPane.session || sorter.count <= 0)
            return -1
        const m = sorterPane.dialog.edit.slides.list
        let y = sorter.topMargin
        let best = -1
        let bestDist = Number.POSITIVE_INFINITY
        for (let i = 0; i < sorter.count; ++i) {
            const row = m.rowAt(i)
            y += sorterPane.sorterRowHeight(row)
            if (row && row.kind === "cue") {
                const dist = Math.abs(contentY - y)
                if (dist < bestDist) {
                    bestDist = dist
                    best = row.cueIndex
                }
            }
            y += sorter.spacing
        }
        return best
    }

    function sorterInsertBeforeAt(contentY) {
        const n = sorterPane.dialog.edit.slides.count
        if (n <= 0)
            return 0
        if (contentY <= 0)
            return 0
        const idx = sorterPane.sorterItemIndexAtY(contentY)
        if (idx < 0)
            return n
        const row = sorterPane.dialog.edit.slides.list.rowAt(idx)
        if (!row || row.slideIndex < 0)
            return n
        if (row.kind === "cue")
            return row.slideIndex
        const top = sorterPane.sorterRowTop(idx)
        const h = sorterPane.sorterRowHeight(row)
        return (contentY - top) > h / 2 ? row.slideIndex + 1 : row.slideIndex
    }

    Keys.onPressed: function (event) {
        if (event.matches(StandardKey.SelectAll) && sorterPane.session) {
            sorterPane.dialog.edit.slides.selectAll()
            event.accepted = true
        }
    }

    function sorterClearDrag() {
        sorterPane.sorterDragging = false
        sorterPane.sorterDropInsertBefore = -1
        sorterPane.sorterDropLastCue = -1
        sorterPane.sorterDragSlide = -1
    }

    function sorterRestoreScroll(y) {
        const restore = function () {
            if (!sorter)
                return
            const maxY = Math.max(0, sorter.contentHeight - sorter.height)
            sorter.contentY = Math.max(0, Math.min(maxY, y))
        }
        restore()
        Qt.callLater(restore)
    }

    function sorterPlaceThumbTo(lastCue) {
        if (!sorterPane.sorterDragging)
            return
        const from = sorterPane.sorterDragSlide
        const y = sorter.contentY
        sorterPane.sorterDragging = false
        sorterPane.sorterDropInsertBefore = -1
        sorterPane.sorterDropLastCue = -1
        if (sorterPane.dialog.placeSlideThumb(from, lastCue))
            sorterPane.sorterRestoreScroll(y)
    }

    function sorterReorderTo(insertBefore) {
        if (!sorterPane.sorterDragging)
            return
        const from = sorterPane.sorterDragSlide
        const y = sorter.contentY
        sorterPane.sorterDragging = false
        sorterPane.sorterDropInsertBefore = -1
        sorterPane.sorterDropLastCue = -1
        if (sorterPane.dialog.reorderSlide(from, insertBefore))
            sorterPane.sorterRestoreScroll(y)
    }

    Connections {
        target: sorterPane.dialog
        function onCueRenameStarted(row) {
            sorter.positionViewAtIndex(row, ListView.Contain)
        }
    }

    // A well, as the stage and the sidebar are.
    color: Style.wellBg
    radius: Style.radiusLarge
    border.color: Style.wellEdge
    clip: true
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        ListView {
            id: sorter
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: sorterPane.sorterListPad
            Layout.rightMargin: sorterPane.sorterListPad
            Layout.topMargin: sorterPane.sorterListPad
            Layout.bottomMargin: sorterPane.sorterListPad
            clip: true
            spacing: (sorterPane.session && sorterPane.dialog.edit.cues.lineControl) ? 3 : 8
            highlightFollowsCurrentItem: false
            interactive: !sorterDrag.pressed && !sorterPane.sorterDragging
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { z: 30 }
            model: sorterPane.session ? sorterPane.dialog.edit.slides.list : null
            delegate: Item {
                id: sortRow
                required property int index
                required property int slideIndex
                required property string label
                required property bool selected
                required property string kind
                required property int cueIndex
                required property string cueText
                /// The cue's chords: ``[{at, label}]`` over ``cueText``'s characters.
                required property var cueChords
                required property bool cueSelected
                required property string cueIndicator
                required property string slideId
                required property string thumbUrl
                readonly property bool cueHover: !sorterPane.sorterDragging
                                                 && sorterPane.sorterHoverIndex === sortRow.index
                readonly property bool hasChords: !!sortRow.cueChords && sortRow.cueChords.length > 0
                /// How far a cue with chords sits its words below the row's
                /// middle, to make room for the chords over them.
                readonly property int chordDrop: sortRow.hasChords ? 6 : 0
                width: sorter.width - Style.scrollGutter(sorter)
                height: sortRow.kind === "cue"
                        ? (sortRow.hasChords ? 42 : 28)
                        : Math.max(36, Math.round(width * sorterPane.dialog.canvasH / Math.max(1, sorterPane.dialog.canvasW)
                                                  * ((sorterPane.session && sorterPane.dialog.edit.cues.lineControl) ? 0.55 : 1)))
                opacity: (sorterPane.sorterDragging && sortRow.kind === "slide"
                          && sorterPane.sorterDragSlide === sortRow.slideIndex) ? 0.4 : 1

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: 2
                    z: 30
                    visible: sorterPane.sorterDragging
                             && !(sorterPane.session && sorterPane.dialog.edit.cues.lineControl)
                             && sorterPane.sorterDropInsertBefore === sortRow.slideIndex
                    color: Style.accent
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 2
                    z: 30
                    visible: sorterPane.sorterDragging
                             && !!(sorterPane.session && sorterPane.dialog.edit.cues.lineControl)
                             && sortRow.kind === "cue"
                             && sortRow.cueIndex === sorterPane.sorterDropLastCue
                    color: Style.accent
                }

                Rectangle {
                    visible: sortRow.kind === "cue"
                    anchors.fill: parent
                    radius: Style.radiusSmall
                    clip: true
                    color: {
                        if (sortRow.cueSelected)
                            return sortRow.cueHover ? Style.lineLiveHover : Style.lineLive
                        if (sortRow.selected)
                            return sortRow.cueHover ? Style.cueRowSelHover : Style.cueRowSel
                        return sortRow.cueHover ? Style.dialogRowHover : Style.dialogRow
                    }
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6
                        Rectangle {
                            id: cueIndBox
                            visible: sortRow.cueIndicator.length > 0
                            width: Math.max(22, indLbl.implicitWidth + 8)
                            height: 16
                            radius: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: sortRow.cueSelected ? Style.cueTagLive : Style.cueTag
                            BadgeText {
                                id: indLbl
                                anchors.centerIn: parent
                                text: sortRow.cueIndicator
                                color: sortRow.cueSelected ? Style.cueTagLiveInk : Style.cueTagInk
                                font.pixelSize: Style.fontMicro
                                font.bold: true
                            }
                        }
                        // The cue's words, and over them its chords like a
                        // lead sheet's: each at the character it falls on,
                        // nudged along only where it would run into the one
                        // before. The words keep their natural spacing.
                        Item {
                            visible: sorterPane.dialog.cueInlineRenameIndex !== sortRow.cueIndex
                            width: parent.width - (cueIndBox.visible
                                   ? (cueIndBox.width + parent.spacing) : 0)
                            height: parent.height
                            Text {
                                id: cueWords
                                width: parent.width
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.verticalCenterOffset: sortRow.chordDrop
                                text: sortRow.cueText.length ? sortRow.cueText : qsTr("(empty)")
                                color: {
                                    if (sortRow.cueSelected)
                                        return Style.textOnAccent
                                    if (sortRow.selected)
                                        return sortRow.cueHover ? Style.lineInkLitHover : Style.lineInkLit
                                    if (!sortRow.cueText.length)
                                        return sortRow.cueHover ? Style.textCaption : Style.textFaint
                                    return sortRow.cueHover ? Style.lineInkLit : Style.text
                                }
                                font.pixelSize: Style.fontBody
                                elide: Text.ElideRight
                                font.italic: !sortRow.cueText.length
                            }
                            FontMetrics {
                                id: cueWordMetrics
                                font: cueWords.font
                            }
                            FontMetrics {
                                id: cueChordMetrics
                                font.pixelSize: Style.fontCaption
                                font.bold: true
                            }
                            Item {
                                visible: sortRow.hasChords
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: cueWords.top
                                anchors.bottomMargin: -1
                                height: cueChordMetrics.height
                                clip: true
                                Repeater {
                                    model: {
                                        if (!sortRow.hasChords)
                                            return []
                                        // The words as the row shows them, tags read.
                                        const words = sortRow.cueText.replace(/<[^>]*>/g, "")
                                            .replace(/&lt;/g, "<").replace(/&amp;/g, "&")
                                        const placed = []
                                        let free = 0
                                        for (let i = 0; i < sortRow.cueChords.length; ++i) {
                                            const c = sortRow.cueChords[i]
                                            const x = Math.max(
                                                cueWordMetrics.advanceWidth(words.slice(0, Number(c.at) || 0)),
                                                free)
                                            free = x + cueChordMetrics.advanceWidth(String(c.label)) + 5
                                            placed.push({ x: x, label: String(c.label) })
                                        }
                                        return placed
                                    }
                                    delegate: Text {
                                        required property var modelData
                                        x: modelData.x
                                        text: modelData.label
                                        // The words' own ink: the chords read as part of the line.
                                        color: cueWords.color
                                        font.pixelSize: Style.fontCaption
                                        font.bold: true
                                    }
                                }
                            }
                        }
                        TextInput {
                            id: cueRenameInput
                            visible: sorterPane.dialog.cueInlineRenameIndex === sortRow.cueIndex
                            width: parent.width - (cueIndBox.visible
                                   ? (cueIndBox.width + parent.spacing) : 0)
                            height: parent.height
                            anchors.verticalCenter: parent.verticalCenter
                            color: Style.textOnAccent
                            font.pixelSize: Style.fontBody
                            verticalAlignment: TextInput.AlignVCenter
                            selectByMouse: true
                            clip: true
                            function startEditing() {
                                if (sortRow.cueIndex < 0
                                        || sorterPane.dialog.cueInlineRenameIndex !== sortRow.cueIndex)
                                    return
                                text = sorterPane.dialog.cueInlineRenameDraft
                                Qt.callLater(function () {
                                    if (sorterPane.dialog.cueInlineRenameIndex !== sortRow.cueIndex)
                                        return
                                    cueRenameInput.forceActiveFocus()
                                    cueRenameInput.selectAll()
                                })
                            }
                            onVisibleChanged: startEditing()
                            // A cue just inserted gets its row already being
                            // renamed: nothing changes for onVisibleChanged.
                            Component.onCompleted: startEditing()
                            onTextChanged: {
                                if (visible)
                                    sorterPane.dialog.cueInlineRenameDraft = text
                            }
                            onActiveFocusChanged: {
                                if (activeFocus) {
                                    sorterPane.dialog.cueInlineRenameFocused = true
                                } else if (sorterPane.dialog.cueInlineRenameFocused
                                           && sorterPane.dialog.cueInlineRenameIndex === sortRow.cueIndex) {
                                    sorterPane.dialog.commitCueInlineRename()
                                }
                            }
                            Keys.onReturnPressed: function (event) {
                                sorterPane.dialog.commitCueInlineRename()
                                event.accepted = true
                            }
                            Keys.onEnterPressed: function (event) {
                                sorterPane.dialog.commitCueInlineRename()
                                event.accepted = true
                            }
                            Keys.onEscapePressed: function (event) {
                                sorterPane.dialog.cancelCueInlineRename()
                                event.accepted = true
                            }
                        }
                    }
                }
                Rectangle {
                    visible: sortRow.kind === "slide"
                    anchors.fill: parent
                    radius: Style.radiusSmall
                    color: Style.slideWell
                    border.width: sortRow.selected ? 2 : 1
                    border.color: sortRow.selected ? Style.accent : Style.edgeQuiet
                    clip: true
                    HoldThumb {
                        anchors.fill: parent
                        anchors.margins: parent.border.width
                        identity: sortRow.slideId
                        source: sortRow.kind === "slide" ? sortRow.thumbUrl : ""
                        fillMode: Image.PreserveAspectCrop
                        sourceSize: {
                            const maxEdge = 320
                            const cw = Math.max(1, sorterPane.dialog.canvasW)
                            const ch = Math.max(1, sorterPane.dialog.canvasH)
                            if (cw >= ch)
                                return Qt.size(maxEdge, Math.max(1, Math.round(maxEdge * ch / cw)))
                            return Qt.size(Math.max(1, Math.round(maxEdge * cw / ch)), maxEdge)
                        }
                    }
                    Text {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: 4
                        text: sortRow.label
                        color: Style.textStrong
                        font.pixelSize: Style.fontCaption
                        font.bold: true
                        style: Text.Outline
                        styleColor: Style.outlineShade
                    }
                }
            }

            MouseArea {
                id: sorterDrag
                anchors.fill: parent
                anchors.rightMargin: Style.scrollGutter(sorter)
                z: 20
                hoverEnabled: true
                preventStealing: true
                propagateComposedEvents: true
                acceptedButtons: Qt.LeftButton
                property real pressY: 0
                property bool pressOnSlide: false
                cursorShape: {
                    if (sorterPane.sorterDragging || (pressed && pressOnSlide))
                        return Qt.ClosedHandCursor
                    const row = sorterPane.session
                                ? sorterPane.dialog.edit.slides.list.rowAt(sorterPane.sorterHoverIndex)
                                : null
                    if (row && row.kind === "slide")
                        return Qt.OpenHandCursor
                    if (row && row.kind === "cue")
                        return Qt.PointingHandCursor
                    return Qt.ArrowCursor
                }
                function contentYAt(mx, my) {
                    return mapToItem(sorter.contentItem, mx, my).y
                }
                function updateHover(mx, my) {
                    sorterPane.sorterHoverIndex = sorterPane.sorterItemIndexAtY(contentYAt(mx, my))
                }
                function updateDrop(mx, my) {
                    const viewY = my
                    MultiSelect.autoScrollView(sorter, viewY, 28, 16)
                    const cy = contentYAt(mx, my)
                    if (sorterPane.session && sorterPane.dialog.edit.cues.lineControl) {
                        sorterPane.sorterDropLastCue = sorterPane.sorterLastCueAt(cy)
                    } else {
                        sorterPane.sorterDropInsertBefore = sorterPane.sorterInsertBeforeAt(cy)
                    }
                }
                onPressed: function (mouse) {
                    const cy = contentYAt(mouse.x, mouse.y)
                    const idx = sorterPane.sorterItemIndexAtY(cy)
                    const row = (sorterPane.session && idx >= 0)
                                ? sorterPane.dialog.edit.slides.list.rowAt(idx) : null
                    if (sorterPane.dialog.cueInlineRenameIndex >= 0) {
                        const same = !!(row && row.kind === "cue"
                                        && row.cueIndex === sorterPane.dialog.cueInlineRenameIndex)
                        if (same) {
                            mouse.accepted = false
                            return
                        }
                        sorterPane.dialog.commitCueInlineRename()
                    }
                    sorterPane.forceActiveFocus()
                    sorterPane.dialog.endTextEdit(true)
                    pressY = mouse.y
                    pressOnSlide = !!(row && row.kind === "slide")
                    sorterPane.sorterDragging = false
                    sorterPane.sorterDropInsertBefore = -1
                    sorterPane.sorterDropLastCue = -1
                    sorterPane.sorterDragSlide = pressOnSlide ? row.slideIndex : -1
                    if (pressOnSlide) {
                        mouse.accepted = true
                        return
                    }
                    if (row && row.kind === "cue") {
                        sorterPane.dialog.edit.cues.select(row.cueIndex)
                        sorterPane.dialog.ribbonTab = "cues"
                        mouse.accepted = true
                        return
                    }
                    mouse.accepted = false
                }
                onPositionChanged: function (mouse) {
                    if (!pressed) {
                        updateHover(mouse.x, mouse.y)
                        return
                    }
                    if (!pressOnSlide)
                        return
                    if (!sorterPane.sorterDragging && Math.abs(mouse.y - pressY) > 6)
                        sorterPane.sorterDragging = true
                    if (sorterPane.sorterDragging)
                        updateDrop(mouse.x, mouse.y)
                }
                onReleased: function (mouse) {
                    if (sorterPane.sorterDragging) {
                        const cy = contentYAt(mouse.x, mouse.y)
                        if (sorterPane.session && sorterPane.dialog.edit.cues.lineControl)
                            sorterPane.sorterPlaceThumbTo(sorterPane.sorterLastCueAt(cy))
                        else
                            sorterPane.sorterReorderTo(sorterPane.sorterInsertBeforeAt(cy))
                    } else if (pressOnSlide && sorterPane.sorterDragSlide >= 0 && sorterPane.session) {
                        if (mouse.modifiers & Qt.ShiftModifier)
                            sorterPane.dialog.edit.slides.extendTo(sorterPane.sorterDragSlide)
                        else
                            sorterPane.dialog.edit.slides.select(sorterPane.sorterDragSlide)
                        sorterPane.sorterClearDrag()
                    } else {
                        sorterPane.sorterClearDrag()
                    }
                    pressOnSlide = false
                }
                onCanceled: {
                    sorterPane.sorterClearDrag()
                    pressOnSlide = false
                }
                onExited: {
                    if (!pressed)
                        sorterPane.sorterHoverIndex = -1
                }
                onDoubleClicked: function (mouse) {
                    const cy = contentYAt(mouse.x, mouse.y)
                    const idx = sorterPane.sorterItemIndexAtY(cy)
                    const row = (sorterPane.session && idx >= 0)
                                ? sorterPane.dialog.edit.slides.list.rowAt(idx) : null
                    if (row && row.kind === "cue") {
                        sorterPane.dialog.beginCueInlineRename(row.cueIndex)
                        mouse.accepted = true
                    }
                }
            }

            WheelHandler {
                onWheel: function (event) {
                    sorter.contentY = Math.max(
                        0,
                        Math.min(Math.max(0, sorter.contentHeight - sorter.height),
                                 sorter.contentY - event.angleDelta.y / 8))
                    event.accepted = true
                }
            }

            Rectangle {
                visible: sorterPane.sorterDragging
                width: sorter.width - Style.scrollGutter(sorter)
                height: 28
                x: 0
                y: Math.max(0, Math.min(sorter.height - 28, sorterDrag.mouseY - 14))
                z: 40
                radius: Style.radiusSmall
                color: Style.sorterDragBg
                border.color: Style.accent
                opacity: 0.92
                Text {
                    anchors.centerIn: parent
                    text: qsTr("Slide %1").arg(sorterPane.sorterDragSlide + 1)
                    color: Style.sorterDragInk
                    font.pixelSize: Style.fontBody
                    font.bold: true
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.rightMargin: Style.scrollGutter(sorter)
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 4
                height: 2
                radius: 0
                color: Style.accent
                z: 30
                visible: sorterPane.sorterDragging
                         && !(sorterPane.session && sorterPane.dialog.edit.cues.lineControl)
                         && sorterPane.sorterDropInsertBefore >= sorterPane.dialog.edit.slides.count
            }
        }
    }

    /// A thumbnail that keeps showing the last picture it loaded until the
    /// next one is ready, so a re-rendered slide never blinks empty.
    component HoldThumb: Item {
        id: hold
        property url source
        property string identity
        property size sourceSize
        property int fillMode: Image.PreserveAspectCrop

        property url readySource
        property string readyIdentity

        Image {
            id: stable
            anchors.fill: parent
            fillMode: hold.fillMode
            sourceSize: hold.sourceSize
            asynchronous: false
            cache: true
            visible: hold.identity.length > 0 && hold.identity === hold.readyIdentity
            source: visible ? hold.readySource : ""
        }
        Image {
            id: loader
            anchors.fill: parent
            fillMode: hold.fillMode
            sourceSize: hold.sourceSize
            asynchronous: true
            cache: true
            source: hold.source
            visible: status === Image.Ready
            function commit() {
                if (status === Image.Ready) {
                    hold.readySource = loader.source
                    hold.readyIdentity = hold.identity
                }
            }
            onStatusChanged: commit()
            onSourceChanged: commit()
        }
    }
}
