import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A row in a pick list (the Libraries & Profiles columns, a song editor
// well): filled, a faint bar at its left edge under the pointer and a bright
// one when selected, a semibold title, an optional quieter second line, and
// on the right a check (``checked``) and an arrow (``trailingArrow``).
// ``trail`` keeps a dim highlight on the row a neighbouring column hangs off.
// A ``code`` (V1, C1, Th) leads the title in its own colour; a second line of
// more than one line (``subtitleLines``: a verse's words) grows the row. An
// ``iconSource`` leads the row, after any ``leading`` item (a check box);
// ``trailing`` controls (a switch, a badge) sit at its right edge over its
// click, so they take their own presses.
//
// A ``draggable`` row reports a drag past a few pixels (``dragStarted``,
// ``dragMoved``, ``dropped``, in its own coordinates) instead of a click; the
// list draws where it would land with ``dropAbove`` / ``dropBelow`` and fades
// the row being dragged with ``lifted``.
Rectangle {
    id: row
    property string title: ""
    property string code: ""
    property color codeInk: Style.chipVerseInk
    property string subtitle: ""
    /// Lines the second line may take: 1 elides, 0 shows it all, wrapped.
    property int subtitleLines: 1
    /// Second-line ink: Style.textWarning for a problem (a missing folder).
    property color subtitleInk: row.selected ? Style.textSoft : Style.textCaption
    property bool selected: false
    property bool trail: false
    property bool checked: false
    property bool trailingArrow: false
    property bool dimmed: false
    property bool draggable: false
    property bool lifted: false
    property bool dropAbove: false
    property bool dropBelow: false
    /// A glyph before the title (a destination's type).
    property string iconSource: ""
    /// What leads the row, under its click.
    property alias leading: leadRow.data
    /// Controls at the row's right edge, pressed for themselves.
    property alias trailing: trailRow.data
    readonly property bool hovered: area.containsMouse
    signal clicked()
    signal doubleClicked()
    signal dragStarted(real x, real y)
    signal dragMoved(real x, real y)
    signal dropped(real x, real y)
    signal dragCanceled()

    // A ListView's row spans the view inside its margins; any other, its parent.
    width: row.ListView.view
           ? row.ListView.view.width - row.ListView.view.leftMargin - row.ListView.view.rightMargin
           : (parent ? parent.width : implicitWidth)
    implicitHeight: row.subtitleLines !== 1 && row.subtitle.length
                    ? Math.max(Style.listRowTallH, lines.implicitHeight + 16)
                    : (row.subtitle.length ? Style.listRowTallH : Style.listRowH)
    radius: Style.radiusSmall
    opacity: row.lifted ? 0.4 : 1
    color: row.trail && !row.selected ? Style.panelRowHover
         : Style.panelRowBg(row.selected, area.containsMouse)

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        radius: 1
        color: row.trail && !row.selected ? Style.listAccentHover
             : Style.listAccent(row.selected, area.containsMouse)
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10 + (trailRow.visible ? trailRow.width + 8 : 0)
        spacing: 8

        Row {
            id: leadRow
            visible: leadRow.children.length > 0
            Layout.alignment: Qt.AlignVCenter
        }
        Image {
            visible: row.iconSource.length > 0
            Layout.preferredWidth: 16
            Layout.preferredHeight: 16
            Layout.alignment: Qt.AlignVCenter
            source: row.iconSource
            sourceSize: Qt.size(16, 16)
        }
        Column {
            id: lines
            Layout.fillWidth: true
            spacing: 1
            Row {
                width: parent.width
                spacing: 6
                Text {
                    id: codeLabel
                    visible: row.code.length > 0
                    text: row.code
                    color: row.codeInk
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    textFormat: Text.PlainText
                }
                Text {
                    width: parent.width - (codeLabel.visible ? codeLabel.width + parent.spacing : 0)
                    text: row.title
                    color: row.dimmed ? Style.textCaption
                         : (row.selected ? Style.textStrong : Style.textBright)
                    font.pixelSize: Style.fontBody
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
            }
            Text {
                width: parent.width
                visible: row.subtitle.length > 0
                text: row.subtitle
                color: row.subtitleInk
                font.pixelSize: Style.fontSmall
                textFormat: Text.PlainText
                wrapMode: row.subtitleLines === 1 ? Text.NoWrap : Text.Wrap
                maximumLineCount: row.subtitleLines > 0 ? row.subtitleLines : 10000
                elide: Text.ElideRight
            }
        }
        Image {
            visible: row.checked
            Layout.preferredWidth: 12
            Layout.preferredHeight: 12
            source: Icons.url("check", Style.textBright, 2.5)
            sourceSize: Qt.size(12, 12)
        }
        Image {
            visible: row.trailingArrow
            Layout.preferredWidth: 10
            Layout.preferredHeight: 10
            source: Icons.url("chevronRight", row.selected || row.trail ? Style.textSoft
                                                                       : Style.textFaint, 2.5)
            sourceSize: Qt.size(10, 10)
        }
    }
    Rectangle {
        visible: row.dropAbove
        anchors.left: parent.left
        anchors.right: parent.right
        y: -2
        height: 2
        radius: 1
        color: Style.dropIndicator
        z: 2
    }
    Rectangle {
        visible: row.dropBelow
        anchors.left: parent.left
        anchors.right: parent.right
        y: parent.height
        height: 2
        radius: 1
        color: Style.dropIndicator
        z: 2
    }
    MouseArea {
        id: area
        property real pressX: 0
        property real pressY: 0
        property bool dragging: false

        anchors.fill: parent
        hoverEnabled: true
        preventStealing: row.draggable
        cursorShape: !row.draggable ? Qt.PointingHandCursor
                   : (pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor)
        onPressed: function (mouse) {
            area.pressX = mouse.x
            area.pressY = mouse.y
            area.dragging = false
        }
        onPositionChanged: function (mouse) {
            if (!pressed || !row.draggable)
                return
            const dx = mouse.x - area.pressX
            const dy = mouse.y - area.pressY
            if (area.dragging) {
                row.dragMoved(mouse.x, mouse.y)
            } else if (dx * dx + dy * dy > 36) {
                area.dragging = true
                row.dragStarted(mouse.x, mouse.y)
            }
        }
        onReleased: function (mouse) {
            if (area.dragging)
                row.dropped(mouse.x, mouse.y)
        }
        onCanceled: {
            if (area.dragging)
                row.dragCanceled()
            area.dragging = false
        }
        onClicked: {
            if (!area.dragging)
                row.clicked()
        }
        onDoubleClicked: row.doubleClicked()
    }
    Row {
        id: trailRow
        visible: children.length > 0
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8
    }
}
