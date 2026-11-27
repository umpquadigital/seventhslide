pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// The operator window's panels and the Controller. DockLayout keeps them as
// a tree of rows and columns and says where each lands (``geometry``); the
// DockPanels and the Controller declared inside take their places from it,
// and this draws the lines between them.
//
// The lines drag to resize at any time; a collapsed panel is its title bar
// (a strip, where it sits in a row), and the others share the rest. In Edit
// Mode (``layout.editMode``) a panel dragged by its title bar (DockPanel)
// shows where it would land with a line along an edge: beside, above or
// below the panel or the Controller under the pointer, on the side the
// pointer is nearest, or along a whole edge of the window near that edge.
Item {
    id: area

    required property DockLayout layout
    /// The least width the Controller asks for (two wells take more than
    /// one); the panels give way to it.
    property int controllerMinWidth: 0

    /// Near this much of the window's edge, a panel dropped runs along all
    /// of it.
    readonly property int windowEdge: 16

    /// { places: { id: { x, y, width, height, strip } }, lines: { key: {
    /// x, y, width, height, resizable, start, total, min, max } } }; the
    /// Controller's place is "controller".
    // The revision is tested, not just read, so it is a dependency even
    // compiled ahead of time: a read whose value goes unused is dropped.
    readonly property var geometry: area.layout.revision >= 0
            ? area.layout.geometry(Math.round(area.width), Math.round(area.height),
                                   Style.dockHeaderH, area.controllerMinWidth)
            : ({ places: {}, lines: {} })

    // A panel being moved by its title, and where it would land.
    property string dragId: ""
    property string dragTitle: ""
    property point dragPoint: Qt.point(0, 0)
    /// { target, edge, line: { x, y, width, height } }: ``target`` a panel,
    /// the Controller, or "" for the whole window, and the line drawn along
    /// that edge of it; null where it would not move.
    property var dropTarget: null

    function beginPanelDrag(id, title) {
        area.dragId = id
        area.dragTitle = title
        area.dropTarget = null
    }

    /// The edge of ``r`` that ``p`` is nearest, each side's distance taken
    /// against the rectangle's size, so a wide panel's top and bottom reach
    /// as far in as its sides.
    function nearestEdge(r, p) {
        const fx = r.width > 0 ? (p.x - r.x) / r.width : 0.5
        const fy = r.height > 0 ? (p.y - r.y) / r.height : 0.5
        const d = [
            { edge: "left", d: fx },
            { edge: "right", d: 1 - fx },
            { edge: "top", d: fy },
            { edge: "bottom", d: 1 - fy }
        ]
        d.sort((a, b) => a.d - b.d)
        return d[0].edge
    }

    /// The line drawn along ``edge`` of ``r``: over the hairline there, a
    /// little short of the corners, kept inside this item.
    function lineOf(r, edge) {
        const t = 3
        const inset = 6
        const across = edge === "left" ? r.x - 2
                     : edge === "right" ? r.x + r.width - 1
                     : edge === "top" ? r.y - 2
                     : r.y + r.height - 1
        if (edge === "left" || edge === "right")
            return {
                x: Math.max(0, Math.min(area.width - t, across)), y: r.y + inset,
                width: t, height: Math.max(0, r.height - 2 * inset)
            }
        return {
            x: r.x + inset, y: Math.max(0, Math.min(area.height - t, across)),
            width: Math.max(0, r.width - 2 * inset), height: t
        }
    }

    /// The pointer at ``p`` (in this item): near the window's edge it is a
    /// drop along that edge; over a panel or the Controller, a drop at the
    /// side of it the pointer is nearest; over the panel moving, nothing.
    function updatePanelDrag(p) {
        area.dragPoint = p
        const whole = { x: 0, y: 0, width: area.width, height: area.height }
        const edges = [
            { edge: "left", d: p.x },
            { edge: "right", d: area.width - p.x },
            { edge: "top", d: p.y },
            { edge: "bottom", d: area.height - p.y }
        ].filter(e => e.d < area.windowEdge)
        if (edges.length) {
            edges.sort((a, b) => a.d - b.d)
            area.dropTarget = {
                target: "", edge: edges[0].edge,
                line: area.lineOf(whole, edges[0].edge)
            }
            return
        }
        const places = area.geometry.places
        for (const id in places) {
            const r = places[id]
            if (p.x < r.x || p.x >= r.x + r.width || p.y < r.y || p.y >= r.y + r.height)
                continue
            if (id === area.dragId) {
                area.dropTarget = null
                return
            }
            const edge = area.nearestEdge(r, p)
            area.dropTarget = { target: id, edge: edge, line: area.lineOf(r, edge) }
            return
        }
        area.dropTarget = null
    }

    function finishPanelDrag() {
        const target = area.dropTarget
        const id = area.dragId
        area.cancelPanelDrag()
        if (target && id.length)
            area.layout.movePanel(id, target.target, target.edge)
    }

    function cancelPanelDrag() {
        area.dragId = ""
        area.dragTitle = ""
        area.dropTarget = null
    }

    // The lines: a hairline, and a handle to drag. A line beside a collapsed
    // panel resizes nothing.
    Repeater {
        // Only where the panels are: a resize must not rebuild the line
        // being dragged.
        model: area.layout.lines
        delegate: Item {
            id: split
            required property var modelData
            readonly property bool vertical: split.modelData.vertical
            readonly property var line: area.geometry.lines[split.modelData.key] ?? null
            readonly property bool resizable: !!split.line && split.line.resizable
            readonly property int grab: 7
            z: 30
            visible: !!split.line

            x: !split.line ? 0 : split.line.x - (split.vertical ? Math.floor(split.grab / 2) : 0)
            y: !split.line ? 0 : split.line.y - (split.vertical ? 0 : Math.floor(split.grab / 2))
            width: !split.line ? 0 : (split.vertical ? split.grab : split.line.width)
            height: !split.line ? 0 : (split.vertical ? split.line.height : split.grab)

            Rectangle {
                x: split.vertical ? Math.floor(split.grab / 2) : 0
                y: split.vertical ? 0 : Math.floor(split.grab / 2)
                width: split.vertical ? 1 : parent.width
                height: split.vertical ? parent.height : 1
                // Lit only in Edit Mode; otherwise the cursor alone says it drags.
                color: area.layout.editMode && (handle.containsMouse || handle.pressed)
                       ? Style.dropIndicator : Style.border
            }
            MouseArea {
                id: handle
                anchors.fill: parent
                enabled: split.resizable
                hoverEnabled: true
                preventStealing: true
                cursorShape: split.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
                property real startPointer: 0
                property int startBefore: 0
                property int total: 0
                property int least: 0
                property int most: 0

                onPressed: function (mouse) {
                    const p = handle.mapToItem(area, mouse.x, mouse.y)
                    const l = split.line
                    handle.startPointer = split.vertical ? p.x : p.y
                    handle.startBefore = (split.vertical ? l.x : l.y) - l.start
                    handle.total = l.total
                    handle.least = l.min
                    handle.most = l.max
                }
                onPositionChanged: function (mouse) {
                    if (!pressed)
                        return
                    const p = handle.mapToItem(area, mouse.x, mouse.y)
                    const moved = (split.vertical ? p.x : p.y) - handle.startPointer
                    const before = Math.max(handle.least,
                                            Math.min(handle.most, handle.startBefore + moved))
                    area.layout.resizeLine(split.modelData.path, split.modelData.before,
                                           split.modelData.after, Math.round(before),
                                           handle.total)
                }
            }
        }
    }

    // Where a moved panel would land.
    Rectangle {
        visible: !!area.dropTarget
        z: 40
        x: visible ? area.dropTarget.line.x : 0
        y: visible ? area.dropTarget.line.y : 0
        width: visible ? area.dropTarget.line.width : 0
        height: visible ? area.dropTarget.line.height : 0
        radius: 2
        color: Style.dropIndicator
    }
    Rectangle {
        visible: area.dragId.length > 0
        z: 45
        x: area.dragPoint.x + 12
        y: area.dragPoint.y + 8
        width: ghostText.implicitWidth + 24
        height: ghostText.implicitHeight + 14
        radius: Style.radius
        color: Style.dockGhostBg
        border.color: Style.dropIndicator
        Text {
            id: ghostText
            anchors.centerIn: parent
            text: area.dragTitle
            color: Style.textStrong
            font.pixelSize: Style.fontBody
            font.bold: true
        }
    }
}
