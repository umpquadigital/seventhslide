pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// One well of the Controller: what one side of the focused stack shows (its
// lines, its slide or image thumbnails, a video, a web page, a live source),
// kept on its current line as that moves, and what works it (the seek strip,
// the address bar, the transport). A Preview's well marks its current line
// as a Program's does; its Blank blanks what is cued, to go live blanked, and
// it clears its cue where a Program's freezes.
ColumnLayout {
    id: well
    spacing: 10

    required property SessionFacade session
    /// The side it shows (``session.program`` or ``session.preview``).
    required property SideView view
    /// The stack's entry on the tab strip (``stacks.occupied``): its blanks
    /// and freeze, which the transport's Blank (a Program's or a Preview's)
    /// and Freeze toggle. Null while nothing is live or cued on it.
    property var stackEntry: null
    /// Keep the current line in view as it moves (the Controller's option).
    property bool autoScroll: true
    /// The arrow keys drive this well (alone, or the one worked beside
    /// another): it marks what comes next, the next line's row green and the
    /// next slide's or image's thumb edged green. The other well does not.
    property bool keyed: false
    /// It has a session and a side to show; a Preview the stack does not have
    /// is no side, and the well shows nothing.
    readonly property bool ready: !!(well.session && well.view)
    /// It shows a Preview: what is cued, not what is live.
    readonly property bool cued: !!(well.view && well.view.preview)
    signal interacted()

    /// Last line / rundown item the view scrolled to, so an unattended
    /// advance scrolls exactly once per move.
    property int lastFollowedCursor: -1
    property double lastFollowedItem: -1
    /// Set by local line/thumb clicks so the follow scroll leaves the list in place.
    property bool skipFollowOnce: false
    /// The side's lines were listed anew (other content, or the same edited)
    /// since the last focusedStackChanged: the list is put on its line, not
    /// scrolled there from the top it was reset to.
    property bool linesReplaced: false
    /// Where the other well's list stood when a take traded the sides
    /// (``scrollState()``): this well now shows what that one did, and puts
    /// its list there. Kept until the change is over (``placeList``), as a
    /// well that showed a video shows its lines a moment after the rest.
    property var pendingScroll: null

    readonly property int lineRowHeight: 36

    /// Video or audio -> seek strip + Play/Pause-Blank-Freeze-Restart transport.
    readonly property bool mediaControls: !!(well.view && well.view.mediaTransport)
    // Constant, so the transport Repeater rebuilds only when the mode flips.
    // A Preview clears its cue where a Program freezes.
    readonly property var slideTransport: well.cued ? ["prev", "blank", "clear", "next"]
                                                    : ["prev", "blank", "freeze", "next"]
    readonly property var mediaTransport: well.cued
                                          ? ["mediaToggle", "blank", "clear", "mediaRestart"]
                                          : ["mediaToggle", "blank", "freeze", "mediaRestart"]
    /// A web page: the address bar, and Previous (the page before, after a
    /// link) / Reload in place of the slide steps, Previous where the
    /// slides' is.
    readonly property bool webControls: !!(well.view && well.view.webMode)
    readonly property var webTransport: well.cued ? ["webBack", "blank", "clear", "webReload"]
                                                  : ["webBack", "blank", "freeze", "webReload"]

    /// The side it shows is blanked: on every output for a Program, at the
    /// take for a Preview.
    readonly property bool blanked: !!(well.stackEntry
                                       && (well.cued ? well.stackEntry.previewBlank
                                                     : well.stackEntry.live && well.stackEntry.blank))
    /// Its Blank works: something is on its side to blank, or it is blanked.
    readonly property bool canBlank: well.ready && !!well.stackEntry
                                     && (well.blanked
                                         || (well.cued ? well.view.occupied : well.stackEntry.live))

    /// Blank the side the well shows: a Program's stack on every output (the
    /// transport's Blank, and Shift+B), or what a Preview has cued, to go
    /// live blanked.
    function toggleBlank() {
        if (!well.canBlank)
            return
        if (well.cued)
            well.session.stacks.togglePreviewBlank(well.stackEntry.id)
        else
            well.session.stacks.toggleStackBlank(well.stackEntry.id)
    }
    /// Freeze the stack on every output (Freeze, Shift+F): a Program's.
    /// Nothing while no stack is live.
    function toggleStackFreeze() {
        if (well.ready && !well.cued && well.stackEntry && well.stackEntry.live)
            well.session.stacks.toggleStackFreeze(well.stackEntry.id)
    }

    /// Drop what is cued (a Preview's Clear); what is live stays.
    function clearCue() {
        if (well.ready && well.cued)
            well.session.stacks.clearPreview(well.session.stacks.focusedId)
    }

    /// ``sec`` as the transport reads it: "1:05", and past an hour
    /// "1:15:03", as the web remote's clock does.
    function formatMediaClock(sec) {
        const t = Math.max(0, Math.floor(Number(sec) || 0))
        const h = Math.floor(t / 3600)
        const m = Math.floor((t % 3600) / 60)
        const s = t % 60
        const ss = (s < 10 ? "0" : "") + s
        if (h > 0) {
            const mm = (m < 10 ? "0" : "") + m
            return h + ":" + mm + ":" + ss
        }
        return m + ":" + ss
    }

    /// Scroll the line list / image gallery to the live item.
    /// Desktop next/prev, auto-advance, and web-remote line changes all go
    /// through here. Local line/thumb clicks set skipFollowOnce instead.
    function followLiveAfterNav(animated) {
        if (!well.autoScroll)
            return
        const anim = animated !== false
        if (lineList.visible)
            lineList.followLiveLine(anim)
        if (galleryView.visible)
            galleryView.followActiveThumb(anim)
    }

    /// Which of the list and the gallery shows, and where it stands (where a
    /// follow under way will stop): ``{kind: "lines" | "gallery", y}``, or
    /// null when neither shows.
    function scrollState() {
        if (lineList.visible)
            return { kind: "lines", y: lineList.restingContentY() }
        if (galleryView.visible)
            return { kind: "gallery", y: galleryView.restingContentY() }
        return null
    }

    /// Take where the other well's list stood (``scrollState()``), for what a
    /// take brought here from it.
    function receiveScroll(state) {
        well.pendingScroll = state
        // Dropped once this change is over; until then every placing uses it.
        if (state)
            Qt.callLater(well.dropPendingScroll)
    }

    /// The take's change is over: back to following the current line.
    function dropPendingScroll() {
        well.pendingScroll = null
    }

    /// The lines were replaced: put the list where it belongs at once,
    /// without scrolling. Where the other well had it, after a take, if this
    /// well lists it the same way; otherwise on the current line, as the
    /// follow would.
    function placeList() {
        const handoff = well.pendingScroll
        if (handoff) {
            if (handoff.kind === "lines" && lineList.visible && lineList.count > 0) {
                lineList.placeAt(handoff.y)
                return
            }
            if (handoff.kind === "gallery" && galleryView.visible && galleryView.count > 0) {
                galleryView.placeAt(handoff.y)
                return
            }
        }
        well.followLiveAfterNav(false)
    }

    /// Jump to a clicked row without re-anchoring the list.
    function jumpToLineFromList(index) {
        if (!well.ready)
            return
        well.skipFollowOnce = true
        if (!well.view.jumpToLine(index))
            well.skipFollowOnce = false
    }

    /// One step through the live item: ``step`` asks the session for it. A
    /// refused step (the item's end, or nothing live) does not move the list.
    function navigate(step) {
        if (!well.ready)
            return false
        well.interacted()
        if (!step())
            return false
        well.followLiveAfterNav(true)
        return true
    }

    function navigateNext() {
        return well.navigate(function () { return well.view.nextSlide() })
    }

    function navigatePrev() {
        return well.navigate(function () { return well.view.prevSlide() })
    }

    /// Give up the keys: the address bar shows where the page is again.
    function clearFieldFocus() {
        addressBar.leave()
    }

    // Keep the live line in view when the cursor moves from auto-advance or the
    // web remote. A local line click sets skipFollowOnce so the list stays put.
    // Other content is put in place rather than scrolled to. The session's
    // signal reaches the side's own first (it is forwarded to it), so the
    // side's properties, and the bindings on them, read what it now shows.
    Connections {
        target: well.session
        enabled: well.ready

        function onFocusedStackChanged() {
            if (!well.ready)
                return
            const cursor = well.view.lineCursor
            const item = well.view.serviceItemId
            const replaced = well.linesReplaced || item !== well.lastFollowedItem
            const moved = cursor !== well.lastFollowedCursor
            const skip = well.skipFollowOnce
            well.skipFollowOnce = false
            well.linesReplaced = false
            well.lastFollowedCursor = cursor
            well.lastFollowedItem = item
            if (replaced || well.pendingScroll)
                well.placeList()
            else if (moved && !skip)
                well.followLiveAfterNav(true)
        }
    }

    // The rows were reset: the views went back to the top with them.
    Connections {
        target: well.ready ? well.view.lines : null

        function onModelReset() {
            well.linesReplaced = true
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        objectName: "wellFace"
        color: Style.wellBg
        border.color: Style.wellEdge
        radius: Style.radius
        clip: true

        readonly property bool imageGallery: !!(well.ready && well.view.imageMode)
        readonly property bool freeformGallery: !!(well.ready && well.view.freeformGallery)
        readonly property bool slideGallery: imageGallery || freeformGallery
        readonly property bool videoPreview: !!(well.ready && well.view.videoMode)
        readonly property bool webPreview: !!(well.ready && well.view.webMode)
        readonly property bool sourcePreview: !!(well.ready && well.view.liveSourceMode)
        /// Audio has no picture and no lines, and the area stays empty rather
        /// than saying nothing is live.
        readonly property bool audioOnly: well.mediaControls && !videoPreview
        readonly property bool singleImagePreview: imageGallery
                                                  && !!(well.ready && well.view.totalLines === 1)

        // Live video.
        MediaPreviewFrame {
            anchors.fill: parent
            anchors.margins: 6
            visible: parent.videoPreview
            liveSession: parent.videoPreview ? well.session : null
            liveView: parent.videoPreview ? well.view : null
            rimColor: Style.accent
        }

        // Live source (a camera), or why it is not showing.
        MediaPreviewFrame {
            objectName: "liveSourcePreview"
            anchors.fill: parent
            anchors.margins: 6
            visible: parent.sourcePreview
            liveKind: "source"
            liveSession: parent.sourcePreview ? well.session : null
            liveView: parent.sourcePreview ? well.view : null
            // Its last picture held: the source has stopped.
            rimColor: well.ready && well.view.liveSourceLost ? Style.signalLost : Style.accent
        }
        Text {
            objectName: "liveSourceProblem"
            anchors.centerIn: parent
            width: parent.width - 32
            visible: parent.sourcePreview && text.length > 0
            text: well.ready ? well.view.liveSourceProblem : ""
            color: Style.textCaption
            font.pixelSize: Style.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            z: 3
        }

        // Live web page, or why it is not showing.
        MediaPreviewFrame {
            objectName: "liveWebPreview"
            anchors.fill: parent
            anchors.margins: 6
            visible: parent.webPreview
            liveKind: "web"
            liveSession: parent.webPreview ? well.session : null
            liveView: parent.webPreview ? well.view : null
            rimColor: Style.accent
        }
        Text {
            objectName: "liveWebProblem"
            anchors.centerIn: parent
            width: parent.width - 32
            visible: parent.webPreview && text.length > 0
            text: {
                if (!well.ready)
                    return ""
                const view = well.view
                return view.webProblem.length ? view.webProblem
                                              : (view.webLoading ? qsTr("Loading…") : "")
            }
            color: Style.textCaption
            font.pixelSize: Style.fontBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            z: 3
        }

        // Lone image. Binding currentImageFilename reloads it when the image is swapped.
        MediaPreviewFrame {
            anchors.fill: parent
            anchors.margins: 6
            visible: parent.singleImagePreview
            rimColor: Style.accent
            source: {
                if (!well.ready || !parent.singleImagePreview)
                    return ""
                const fn = well.view.currentImageFilename
                return fn.length ? well.session.media.mediaFileUrl(MediaKind.Image, fn, well.session.media.filesRevision)
                                 : ""
            }
        }

        // Slide and image thumbnails: ~220px cells in the primary output's
        // shape, index strip, accent border on the live one. A lone image
        // uses MediaPreviewFrame instead.
        GridView {
            id: galleryView
            objectName: "wellGallery"
            // Cells carry half the gap on each side, so the view sits half a gap
            // outside the well padding and the thumbs still line up with it.
            readonly property int gap: 6
            // Same upper-third anchor as the line list.
            readonly property real followAnchor: 0.37
            // Width the thumbs span, without the two outer half-gaps.
            readonly property int layoutW: width - gap
            readonly property int columnCount: {
                const minCell = Math.min(220, Math.max(1, layoutW))
                return Math.max(1, Math.floor((layoutW + gap) / (minCell + gap)))
            }
            // The slides' shape: the primary output's, no taller than 9:16.
            readonly property real slideAspect:
                well.ready ? Math.max(9 / 16, well.session.outputPreviews.slideAspect) : 16 / 9
            // From the gutter-free width, so the scrollbar appearing cannot
            // shrink rows and toggle overflow.
            readonly property int thumbH:
                Math.max(1, Math.round((layoutW - gap * (columnCount - 1)) / columnCount
                                       / galleryView.slideAspect))
            // The scrollbar gutter narrows the thumbs inside their cells rather
            // than the cells: cellWidth feeding contentHeight would loop.
            readonly property real thumbW:
                Math.max(1, (width - Style.scrollGutter(galleryView) - gap * columnCount) / columnCount)

            anchors.fill: parent
            anchors.margins: 6 - gap / 2
            visible: (parent.imageGallery && !parent.singleImagePreview) || parent.freeformGallery
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            // Floored to 1/16 px so GridView always lays out columnCount columns.
            cellWidth: Math.max(1, Math.floor(width / columnCount * 16) / 16)
            cellHeight: thumbH + gap
            model: visible && well.ready ? well.view.lines : null
            ScrollBar.vertical: AppScrollBar {}

            NumberAnimation {
                id: galleryFollowAnim
                target: galleryView
                property: "contentY"
                duration: 220
                easing.type: Easing.OutCubic
            }

            /// The furthest it scrolls, from its rows rather than
            /// contentHeight, which a reset leaves stale until the view lays
            /// out again.
            function maxContentY() {
                return Math.max(0, Math.ceil(count / columnCount) * cellHeight - height)
            }

            /// Where it stands, or where the follow under way will stop.
            function restingContentY() {
                return galleryFollowAnim.running ? galleryFollowAnim.to : contentY
            }

            /// Put it at ``y`` at once.
            function placeAt(y) {
                galleryFollowAnim.stop()
                contentY = Math.max(0, Math.min(y, maxContentY()))
            }

            function followActiveThumb(animated) {
                if (!well.autoScroll || !visible || !well.ready)
                    return
                const idx = well.view.lineCursor
                if (idx < 0 || height <= 0 || count <= 0)
                    return
                const top = Math.floor(idx / columnCount) * cellHeight + gap / 2
                const ideal = top - (height - thumbH) * followAnchor
                const target = Math.max(0, Math.min(ideal, maxContentY()))
                galleryFollowAnim.stop()
                if (!animated || Math.abs(contentY - target) < 0.5) {
                    contentY = target
                    return
                }
                galleryFollowAnim.from = contentY
                galleryFollowAnim.to = target
                galleryFollowAnim.start()
            }

            onMovementStarted: galleryFollowAnim.stop()
            // Its thumbs listed anew, in place at once: a reset says
            // countChanged, a model newly given (the view shown) says
            // modelChanged. Both place, so the order between them is moot.
            onModelChanged: if (visible && count > 0)
                                well.placeList()
            onCountChanged: if (visible && count > 0)
                                well.placeList()
            onWidthChanged: if (visible)
                                Qt.callLater(galleryView.followActiveThumb, false)

            delegate: Item {
                id: cell
                required property int index
                required property int lineIndex
                required property string text
                required property string label
                required property bool isCurrent
                required property bool isNext
                /// The slide's build steps, and which is live.
                required property int steps
                required property int currentStep

                width: galleryView.cellWidth
                height: galleryView.cellHeight

                Rectangle {
                    objectName: "wellThumb"
                    // Pull each column left by what the gutter took from the cells before it.
                    x: galleryView.gap / 2 + (cell.index % galleryView.columnCount)
                       * (galleryView.thumbW + galleryView.gap - galleryView.cellWidth)
                    y: galleryView.gap / 2
                    width: galleryView.thumbW
                    height: galleryView.thumbH
                    radius: Style.radiusSmall
                    color: Style.subheaderBg
                    border.width: 2
                    border.color: {
                        if (cell.isCurrent)
                            return Style.accent
                        if (cell.isNext && well.keyed)
                            return Style.thumbNext
                        return thumbHover.containsMouse ? Style.edgeStrong : "transparent"
                    }
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: {
                            if (!well.ready)
                                return ""
                            const files = well.session.media.filesRevision
                            if (well.view.imageMode)
                                return well.session.media.mediaThumbUrl(MediaKind.Image, cell.text, files)
                            return well.session.outputPreviews.slideThumbUrl(
                                well.view, cell.lineIndex, files)
                        }
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                        sourceSize: {
                            const reqW = Math.max(1, Math.round(width * 2))
                            const reqH = Math.max(1, Math.round(height * 2))
                            const scale = Math.min(1, 512 / Math.max(reqW, reqH))
                            return Qt.size(Math.max(1, Math.round(reqW * scale)),
                                           Math.max(1, Math.round(reqH * scale)))
                        }
                    }

                    // Index strip (web `.img-thumb-num`). Over the
                    // thumb's click area, so its step marks take theirs.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 18
                        z: 3
                        color: Style.thumbLabelBg

                        BadgeText {
                            anchors.centerIn: parent
                            text: cell.label.length ? cell.label : String(cell.lineIndex + 1)
                            color: Style.textBright
                            font.pixelSize: Style.fontSmall
                            font.weight: Font.Medium
                        }
                        // The slide's steps: one mark each, those played
                        // filled on the live slide; a mark goes to its step.
                        Row {
                            objectName: "slideSteps"
                            visible: cell.steps > 1
                            anchors.right: parent.right
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 3
                            z: 3
                            Repeater {
                                model: cell.steps > 1 ? cell.steps : 0
                                delegate: Rectangle {
                                    id: mark
                                    required property int index
                                    width: 7
                                    height: 7
                                    radius: 3.5
                                    color: cell.isCurrent && mark.index <= cell.currentStep
                                           ? Style.textBright : "transparent"
                                    border.color: Style.textBright
                                    border.width: 1
                                    MouseArea {
                                        anchors.fill: parent
                                        anchors.margins: -3
                                        cursorShape: Qt.PointingHandCursor
                                        onPressed: well.interacted()
                                        onClicked: well.view.jumpToStep(cell.lineIndex, mark.index)
                                    }
                                }
                            }
                        }
                    }

                    // Soft accent rim on the live thumb.
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: "transparent"
                        border.width: 2
                        border.color: Style.liveThumbEdge
                        visible: cell.isCurrent
                        z: 4
                    }

                    MouseArea {
                        id: thumbHover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onPressed: well.interacted()
                        onClicked: well.jumpToLineFromList(cell.lineIndex)
                    }
                }
            }
        }

        ListView {
            id: lineList
            objectName: "wellLines"
            anchors.fill: parent
            anchors.margins: 8
            visible: !parent.slideGallery && !parent.videoPreview && !parent.webPreview
                     && !parent.sourcePreview
            clip: true
            model: visible && well.ready ? well.view.lines : null
            spacing: 3
            currentIndex: well.ready ? well.view.lineCursor : -1
            ScrollBar.vertical: AppScrollBar {}

            NumberAnimation {
                id: followAnim
                target: lineList
                property: "contentY"
                duration: 280
                easing.type: Easing.OutCubic
            }

            // Keep the live line in the upper third (~37% from top) so more
            // upcoming lines stay visible below (vs dead-center).
            readonly property real followAnchor: 0.37

            /// The furthest it scrolls, from its rows (all one height)
            /// rather than contentHeight, which a reset leaves stale until
            /// the view lays out again.
            function maxContentY() {
                return Math.max(0, count * (well.lineRowHeight + spacing) - spacing - height)
            }

            /// Where it stands, or where the follow under way will stop.
            function restingContentY() {
                return followAnim.running ? followAnim.to : contentY
            }

            /// Put it at ``y`` at once.
            function placeAt(y) {
                followAnim.stop()
                contentY = Math.max(0, Math.min(y, maxContentY()))
            }

            function centeredContentY(index) {
                if (index < 0 || count <= 0 || height <= 0)
                    return contentY
                const itemH = well.lineRowHeight
                const stride = itemH + spacing
                const ideal = index * stride - (height - itemH) * followAnchor
                return Math.max(0, Math.min(ideal, maxContentY()))
            }

            function followLiveLine(animated) {
                // Read the session cursor, not ListView.currentIndex: a
                // follow from onFocusedStackChanged can run before the
                // binding has applied and would scroll to the previous line.
                const index = well.ready ? well.view.lineCursor : -1
                if (!well.autoScroll || index < 0 || count <= 0)
                    return
                const target = centeredContentY(index)
                if (!animated || Math.abs(contentY - target) < 0.5) {
                    followAnim.stop()
                    contentY = target
                    return
                }
                followAnim.stop()
                followAnim.from = contentY
                followAnim.to = target
                followAnim.start()
            }

            onMovementStarted: followAnim.stop()
            // Its lines listed anew: in place at once, as the gallery.
            onModelChanged: if (visible && count > 0)
                                well.placeList()
            onCountChanged: if (visible && count > 0)
                                well.placeList()

            delegate: Rectangle {
                id: row
                required property int lineIndex
                required property string text
                required property string richText
                required property string label
                required property bool isCurrent
                required property bool isVisible
                required property bool isNext
                /// Marked next: only in the well the keys drive.
                readonly property bool marksNext: row.isNext && well.keyed

                width: lineList.width - Style.scrollGutter(lineList)
                height: well.lineRowHeight
                // Shared ink for label and lyric; hover brightens the whole row.
                readonly property color lineInk: {
                    if (row.isCurrent)
                        return Style.textOnAccent
                    // Next and on-output lines share the brighter ink.
                    if (row.marksNext || row.isVisible)
                        return lineHover.containsMouse ? Style.lineInkLitHover : Style.lineInkLit
                    return lineHover.containsMouse ? Style.lineInkHover : Style.lineInk
                }
                // The live lyric is DemiBold; the label is already bold.
                readonly property int lineWeight: row.isCurrent ? Font.DemiBold
                                                                : Font.Normal
                color: {
                    // Live blue kept a touch darker than Style.accent so the
                    // row sits quieter against the panel chrome.
                    if (row.isCurrent)
                        return lineHover.containsMouse ? Style.lineLiveHover : Style.lineLive
                    if (row.marksNext)
                        return lineHover.containsMouse ? Style.lineNextHover : Style.lineNext
                    if (row.isVisible)
                        return lineHover.containsMouse ? Style.lineShownHover : Style.lineShown
                    return lineHover.containsMouse ? Style.lineIdleHover : Style.lineIdle
                }
                radius: Style.radiusSmall
                clip: true

                RowLayout {
                    anchors.fill: parent
                    spacing: 0

                    // Verse / section label.
                    Rectangle {
                        Layout.preferredWidth: 42
                        Layout.fillHeight: true
                        color: "transparent"

                        BadgeText {
                            anchors.centerIn: parent
                            width: parent.width - 4
                            horizontalAlignment: Text.AlignHCenter
                            text: row.label.toUpperCase()
                            color: row.lineInk
                            font.pixelSize: Style.fontCaption
                            font.bold: true
                            font.letterSpacing: 0.3
                            elide: Text.ElideRight
                        }

                        Rectangle {
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.right: parent.right
                            width: 1
                            color: {
                                if (row.isCurrent)
                                    return Style.lineRuleLive
                                if (row.marksNext)
                                    return Style.lineRuleNext
                                if (row.isVisible)
                                    return Style.rowHoverBar
                                return Style.lineRuleIdle
                            }
                        }
                    }

                    Text {
                        text: row.text.length ? row.richText : qsTr("(empty)")
                        textFormat: Text.StyledText
                        color: row.text.length ? row.lineInk : Style.textMuted
                        font.pixelSize: Style.fontTitle
                        font.weight: row.lineWeight
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.leftMargin: 10
                        Layout.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        opacity: row.text.length ? 1.0 : 0.5
                    }
                }

                MouseArea {
                    id: lineHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onPressed: well.interacted()
                    onClicked: well.jumpToLineFromList(row.lineIndex)
                }
            }
        }

        // A press anywhere on the well is working it (beside another well,
        // the keys follow it). Over everything, and it declines the press, so
        // what is under it (a line, a thumb, the list's scrolling) still has
        // it.
        MouseArea {
            anchors.fill: parent
            z: 10
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: function (mouse) {
                well.interacted()
                mouse.accepted = false
            }
        }

        // An empty gallery says what it lacks.
        Text {
            anchors.centerIn: parent
            visible: well.ready && parent.slideGallery && !parent.singleImagePreview
                     && well.view.totalLines <= 0
            text: well.ready && well.view.imageMode ? qsTr("No images")
                                                              : qsTr("No slides")
            color: Style.textMuted
            font.pixelSize: Style.fontUi
        }

        // Nothing live, or audio, which has no picture and no lines: the
        // app's mark, faint.
        Image {
            anchors.centerIn: parent
            visible: {
                if (!well.ready || parent.audioOnly)
                    return true
                if (parent.videoPreview || parent.webPreview || parent.sourcePreview
                        || parent.slideGallery)
                    return false
                return lineList.count === 0
            }
            // The mark's own proportions, 902 by 626.
            width: Math.round(Math.min(parent.width * 0.2, 124))
            height: Math.round(width * 626 / 902)
            sourceSize: Qt.size(width, height)
            source: visible ? Icons.url("logo", Style.wellMark) : ""
        }
    }

    // Transport: slides get Previous/Blank/Freeze/Next; video adds the seek
    // strip and swaps in Play-Pause/Restart.

    // Video seek strip: position, slider, duration.
    ControllerSeekBar {
        well: well
    }

    // A web page's address, where the seek strip is for video.
    ControllerAddressBar {
        id: addressBar
        well: well
    }

    ControllerTransportBar {
        well: well
    }
}
