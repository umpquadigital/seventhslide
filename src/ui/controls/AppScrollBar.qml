import QtQuick
import QtQuick.Controls
import SeventhSlide

ScrollBar {
    id: control
    // Space callers inset via Style.scrollGutter - assigned, not bound, so
    // Flickable contentWidth/delegate width cannot loop through overflow.
    property int reservedWidth: 0
    property int reservedHeight: 0
    /// Extra space between content and the scrollbar track (0 = flush).
    property int contentGap: Style.scrollGap
    /// Flickable bottom/right margin we last wrote (0 if none).
    property int injectedPad: 0
    /// Cap on the painted thumb as a fraction of the track. Native size is
    /// used when it is already smaller (long lists). Prevents a slab handle
    /// when content barely overflows.
    property real maximumSize: 0.28
    /// The view this bar scrolls. Always attached to a Flickable (ListView and
    /// GridView included); null only while unparented.
    readonly property Flickable flick: control.parent as Flickable
    /// The operator is dragging it.
    readonly property alias dragging: barDrag.pressed

    // Persistently visible when content overflows; hidden when it fits.
    // Compare content vs viewport (epsilon) rather than control.size, which
    // jitters as a float during layout and can hang hidden dialogs.
    readonly property bool overflowing: {
        const flick = control.flick
        if (!flick)
            return false
        if (control.orientation === Qt.Vertical)
            return flick.contentHeight > flick.height + 0.5
        return flick.contentWidth > flick.width + 0.5
    }
    // Keep the bar painted while a gutter is reserved (sticky 16:9 lists).
    readonly property bool barShown: overflowing || reservedWidth > 0 || reservedHeight > 0
    policy: barShown ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
    implicitWidth: orientation === Qt.Vertical ? Style.scrollSize : 100
    implicitHeight: orientation === Qt.Horizontal ? Style.scrollSize : 100
    minimumSize: 0.12
    padding: 2
    // Qt's handle drag uses native size; a capped thumb would fly ahead of
    // the cursor. Pointer mapping is handled below against the painted size.
    interactive: false

    readonly property real visualFrac: Math.min(
        Math.max(control.size, control.minimumSize), control.maximumSize)
    readonly property real trackLen: control.orientation === Qt.Vertical
                                     ? Math.max(1, control.availableHeight)
                                     : Math.max(1, control.availableWidth)
    readonly property real visualLen: visualFrac * trackLen

    function visualStart() {
        const span = 1 - control.size
        const travel = Math.max(0, trackLen - visualLen)
        if (span <= 0)
            return 0
        return (control.position / span) * travel
    }

    function setPositionFromPointer(v, grabOffset) {
        const pad = control.orientation === Qt.Vertical ? control.topPadding
                                                        : control.leftPadding
        const travel = Math.max(1, trackLen - visualLen)
        const t = Math.min(1, Math.max(0, (v - pad - grabOffset) / travel))
        control.position = t * Math.max(0, 1 - control.size)
    }

    function setInjectedPad(flick, px) {
        px = Math.max(0, Math.round(Number(px) || 0))
        if (!flick)
            return
        const isVert = control.orientation === Qt.Vertical
        const current = isVert ? flick.bottomMargin : flick.rightMargin
        if (current !== 0 && current !== injectedPad)
            return
        if (injectedPad === px && current === px)
            return
        injectedPad = px
        if (isVert)
            flick.bottomMargin = px
        else
            flick.rightMargin = px
    }

    function syncReserved() {
        const space = Style.scrollSize + control.contentGap
        const flick = control.flick
        if (control.orientation === Qt.Vertical) {
            if (overflowing) {
                setInjectedPad(flick, 0)
                if (reservedWidth !== space)
                    reservedWidth = space
                if (reservedHeight !== 0)
                    reservedHeight = 0
                return
            }
            // Keep the gutter if restoring width would grow 16:9 content
            // back into overflow (card height scales with width).
            if (reservedWidth > 0 && flick && flick.width > 0) {
                const extra = reservedWidth * flick.contentHeight / flick.width
                if (flick.contentHeight + extra > flick.height + 0.5) {
                    const pad = Math.max(
                        0, flick.height + Style.scrollMinOverflow - flick.contentHeight)
                    setInjectedPad(flick, pad)
                    return
                }
            }
            setInjectedPad(flick, 0)
            if (reservedWidth !== 0)
                reservedWidth = 0
            if (reservedHeight !== 0)
                reservedHeight = 0
        } else {
            if (overflowing) {
                setInjectedPad(flick, 0)
                if (reservedHeight !== space)
                    reservedHeight = space
                if (reservedWidth !== 0)
                    reservedWidth = 0
                return
            }
            // Horizontal gutters do not feed 16:9 card height, so they must
            // not stick the way the vertical reservedWidth does.
            setInjectedPad(flick, 0)
            if (reservedHeight !== 0)
                reservedHeight = 0
            if (reservedWidth !== 0)
                reservedWidth = 0
        }
    }

    /// Watched by on_FlickExtentChanged: the view or its content resizing
    /// re-checks the gutter.
    readonly property real _flickExtent: {
        const flick = control.flick
        if (!flick)
            return 0
        if (control.orientation === Qt.Vertical)
            return flick.height + flick.contentHeight
        return flick.width + flick.contentWidth
    }

    // Deferred, and coalesced: reserving the gutter narrows the rows, which
    // can change the content height ``overflowing`` reads. Run from inside
    // its change notification, that re-entered the binding (a binding loop
    // on "overflowing" when a 16:9 list resized). A timer rather than
    // Qt.callLater: it dies with the bar, so a list delegate destroyed before
    // the sync runs (a rebuilt rail) never runs it against a torn-down object.
    Timer {
        id: syncTimer
        interval: 0
        onTriggered: control.syncReserved()
    }
    function scheduleSync() {
        syncTimer.restart()
    }
    onOverflowingChanged: scheduleSync()
    onFlickChanged: scheduleSync()
    on_FlickExtentChanged: scheduleSync()
    onContentGapChanged: scheduleSync()
    Component.onCompleted: syncReserved()

    background: Rectangle {
        color: Style.scrollTrack
        visible: control.barShown
    }

    // Qt sizes this item to control.size. Paint a possibly shorter/narrower
    // thumb inside it and remap along the track so a capped handle still
    // travels from start to end - pointer mapping uses visualLen so the
    // handle stays under the cursor.
    contentItem: Item {
        implicitWidth: 6
        implicitHeight: 6
        visible: control.barShown

        Rectangle {
            id: thumb
            radius: Style.radius
            color: (barDrag.pressed || barDrag.overThumb) ? Style.scrollThumbHover
                                                          : Style.scrollThumb

            width: control.orientation === Qt.Vertical ? parent.width : control.visualLen
            height: control.orientation === Qt.Vertical ? control.visualLen : parent.height

            x: {
                if (control.orientation === Qt.Vertical)
                    return 0
                const span = 1 - control.size
                if (span <= 0 || parent.width <= control.visualLen)
                    return 0
                return (control.position / span) * (parent.width - control.visualLen)
            }
            y: {
                if (control.orientation === Qt.Horizontal)
                    return 0
                const span = 1 - control.size
                if (span <= 0 || parent.height <= control.visualLen)
                    return 0
                return (control.position / span) * (parent.height - control.visualLen)
            }
        }
    }

    MouseArea {
        id: barDrag
        anchors.fill: parent
        z: 10
        enabled: control.barShown
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton

        property real grabOffset: 0
        property bool overThumb: false

        function pointerPos(mouse) {
            return control.orientation === Qt.Vertical ? mouse.y : mouse.x
        }

        function thumbContains(v) {
            const pad = control.orientation === Qt.Vertical ? control.topPadding
                                                            : control.leftPadding
            const start = pad + control.visualStart()
            return v >= start && v < start + control.visualLen
        }

        onPressed: function (mouse) {
            const v = pointerPos(mouse)
            if (thumbContains(v)) {
                const pad = control.orientation === Qt.Vertical ? control.topPadding
                                                                : control.leftPadding
                grabOffset = v - pad - control.visualStart()
            } else {
                grabOffset = control.visualLen / 2
            }
            control.setPositionFromPointer(v, grabOffset)
        }
        onPositionChanged: function (mouse) {
            overThumb = thumbContains(pointerPos(mouse))
            if (pressed)
                control.setPositionFromPointer(pointerPos(mouse), grabOffset)
        }
        onReleased: function (mouse) {
            overThumb = thumbContains(pointerPos(mouse))
        }
        onCanceled: overThumb = false
        onExited: if (!pressed)
            overThumb = false
        onEntered: overThumb = thumbContains(
            control.orientation === Qt.Vertical ? mouseY : mouseX)

    }
}
