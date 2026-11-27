pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// Shared stage overlay: hit-test, marquee, multi-drag/resize, snap guides.
// The host paints the objects; this layer is the pointer chrome both editors
// were duplicating in JavaScript.
Item {
    id: root

    property int canvasWidth: 1920
    property int canvasHeight: 1080
    property real scaleFactor: 1
    /// Back-to-front ``{id,x,y,w,h,rotation,locked}`` in canvas pixels.
    property var objects: []
    property string selectedId: ""
    /// When empty, ``selectedId`` is treated as a single-item selection.
    property var selectedIds: []
    /// When set, clicks inside that box are left for the in-place TextEdit.
    property string editingId: ""
    property int snapThreshold: 10
    property int handleHit: 10
    /// Above painted objects while idle (Freeform z values are 0, 10, 20, ...);
    /// under the in-place TextEdit while typing.
    z: root.editingId.length ? 1 : 10000

    readonly property bool dragging: intern.moved && intern.handle.length > 0
                                     && intern.handle !== "pending"
                                     && intern.handle !== "marquee"
    readonly property string handle: intern.handle
    readonly property real snapV: intern.snapV
    readonly property real snapH: intern.snapH
    readonly property var preview: intern.preview
    readonly property var previewRects: intern.previewRects

    signal interactionBegan()
    signal selected(string objectId, bool additive)
    signal marqueeSelected(var ids, bool additive)
    signal cleared()
    signal committed(string objectId, real x, real y, real w, real h, real rotation)
    signal committedSet(var geometries)
    signal doubleClicked(string objectId)
    signal dragFinished(bool moved)

    CanvasInteract {
        id: tools
        canvasWidth: root.canvasWidth
        canvasHeight: root.canvasHeight
        snapThreshold: root.snapThreshold
        handleHit: root.handleHit
        objects: root.objects
    }

    QtObject {
        id: intern
        property string dragId: ""
        property string handle: ""
        property real startMx: 0
        property real startMy: 0
        property var startRect: null
        property var startRects: []
        property bool moved: false
        property var preview: null
        property var previewRects: []
        property real snapV: -1
        property real snapH: -1
        property bool additive: false
        property real marqueeX1: 0
        property real marqueeY1: 0
        property real marqueeX2: 0
        property real marqueeY2: 0
        readonly property real slop: 3
    }

    function effectiveSelectedIds() {
        if (root.selectedIds && root.selectedIds.length)
            return root.selectedIds
        if (root.selectedId && root.selectedId.length)
            return [root.selectedId]
        return []
    }

    function isSelected(objectId) {
        const ids = root.effectiveSelectedIds()
        for (let i = 0; i < ids.length; ++i) {
            if (ids[i] === objectId)
                return true
        }
        return false
    }

    function rectOf(objectId) {
        const previews = intern.previewRects || []
        for (let i = 0; i < previews.length; ++i) {
            if (previews[i] && previews[i].id === objectId)
                return previews[i]
        }
        if (intern.preview && intern.preview.id === objectId)
            return intern.preview
        const objs = root.objects || []
        for (let i = 0; i < objs.length; ++i) {
            if (objs[i] && objs[i].id === objectId)
                return objs[i]
        }
        return null
    }

    function selectedRects() {
        const ids = root.effectiveSelectedIds()
        const out = []
        for (let i = 0; i < ids.length; ++i) {
            const r = root.rectOf(ids[i])
            if (r)
                out.push(Object.assign({}, r))
        }
        return out
    }

    function selectionLocked() {
        const rects = root.selectedRects()
        for (let i = 0; i < rects.length; ++i) {
            if (rects[i].locked)
                return true
        }
        return false
    }

    function unionHandleRect() {
        const rects = root.selectedRects()
        if (rects.length < 2)
            return null
        return tools.unionBounds(rects)
    }

    function stageScale() {
        return root.scaleFactor > 0 ? root.scaleFactor : 1
    }

    function additiveFrom(mouse) {
        return !!(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier))
    }

    function publishPreview(rects) {
        intern.previewRects = rects
        intern.preview = rects.length ? rects[0] : null
    }

    function finish() {
        const rects = intern.previewRects
        const preview = intern.preview
        const moved = intern.moved
        const id = intern.dragId
        const marquee = intern.handle === "marquee"
        const additive = intern.additive
        const mx1 = intern.marqueeX1
        const my1 = intern.marqueeY1
        const mx2 = intern.marqueeX2
        const my2 = intern.marqueeY2
        intern.dragId = ""
        intern.handle = ""
        intern.startRect = null
        intern.startRects = []
        intern.moved = false
        intern.snapV = -1
        intern.snapH = -1
        intern.preview = null
        intern.previewRects = []
        intern.additive = false
        if (marquee) {
            if (!moved) {
                if (!additive)
                    root.cleared()
            } else {
                const ids = tools.hitMarquee(mx1, my1, mx2 - mx1, my2 - my1)
                root.marqueeSelected(ids, additive)
            }
            root.dragFinished(moved)
            return
        }
        if (moved && rects && rects.length) {
            root.committedSet(rects)
            if (rects.length === 1 && id) {
                const p = rects[0]
                root.committed(id, p.x, p.y, p.w, p.h, p.rotation || 0)
            }
        } else if (moved && preview && id) {
            root.committed(id, preview.x, preview.y, preview.w, preview.h,
                           preview.rotation || 0)
            root.committedSet([preview])
        }
        root.dragFinished(moved)
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        z: root.editingId.length ? 1 : 10
        cursorShape: {
            if (intern.handle === "marquee")
                return Qt.CrossCursor
            if (intern.handle.length)
                return tools.cursorFor(intern.handle)
            if (root.editingId.length)
                return Qt.ArrowCursor
            const sc = root.stageScale()
            const ids = root.effectiveSelectedIds()
            if (ids.length > 1) {
                const u = root.unionHandleRect()
                if (u) {
                    const h = tools.hitHandle(mouseX, mouseY, u, sc)
                    if (h.length)
                        return root.selectionLocked() ? Qt.ForbiddenCursor : tools.cursorFor(h)
                }
            } else if (ids.length === 1) {
                const r = root.rectOf(ids[0])
                if (r) {
                    const h = tools.hitHandle(mouseX, mouseY, r, sc)
                    if (h.length)
                        return r.locked ? Qt.ForbiddenCursor : tools.cursorFor(h)
                    if (tools.pointInRect(mouseX / sc, mouseY / sc, r))
                        return r.locked ? Qt.ForbiddenCursor : Qt.SizeAllCursor
                }
            }
            return Qt.ArrowCursor
        }
        onPressed: function (mouse) {
            const sc = root.stageScale()
            const cx = mouse.x / sc
            const cy = mouse.y / sc
            if (root.editingId.length) {
                const er = root.rectOf(root.editingId)
                if (er && tools.pointInRect(cx, cy, er))
                    return
            }
            root.interactionBegan()
            intern.additive = root.additiveFrom(mouse)
            intern.startMx = mouse.x
            intern.startMy = mouse.y
            intern.moved = false
            intern.preview = null
            intern.previewRects = []
            intern.snapV = -1
            intern.snapH = -1

            const ids = root.effectiveSelectedIds()
            if (ids.length > 1 && !intern.additive) {
                const u = root.unionHandleRect()
                if (u) {
                    const h = tools.hitHandle(mouse.x, mouse.y, u, sc)
                    if (h.length) {
                        intern.dragId = ids[0]
                        intern.handle = h
                        intern.startRects = root.selectedRects()
                        intern.startRect = intern.startRects.length ? intern.startRects[0] : null
                        return
                    }
                }
            } else if (ids.length === 1) {
                const r = root.rectOf(ids[0])
                if (r) {
                    const h = tools.hitHandle(mouse.x, mouse.y, r, sc)
                    if (h.length) {
                        intern.dragId = ids[0]
                        intern.handle = h
                        intern.startRect = Object.assign({ id: ids[0] }, r)
                        intern.startRects = [intern.startRect]
                        return
                    }
                }
            }

            const hitId = tools.hitObject(cx, cy)
            if (hitId.length) {
                if (intern.additive) {
                    intern.dragId = ""
                    intern.handle = ""
                    intern.startRects = []
                    root.selected(hitId, true)
                    return
                }
                if (!root.isSelected(hitId))
                    root.selected(hitId, false)
                intern.dragId = hitId
                intern.handle = "pending"
                intern.startRects = root.selectedRects()
                if (!intern.startRects.length) {
                    const r = root.rectOf(hitId)
                    intern.startRects = r ? [Object.assign({ id: hitId }, r)] : []
                }
                intern.startRect = intern.startRects.length ? intern.startRects[0]
                                  : Object.assign({ id: hitId }, root.rectOf(hitId) || {})
                return
            }

            intern.dragId = ""
            intern.handle = "marquee"
            intern.marqueeX1 = cx
            intern.marqueeY1 = cy
            intern.marqueeX2 = cx
            intern.marqueeY2 = cy
            intern.startRects = []
            intern.startRect = null
        }
        onPositionChanged: function (mouse) {
            if (!pressed)
                return
            const sc = root.stageScale()
            if (intern.handle === "marquee") {
                intern.marqueeX2 = mouse.x / sc
                intern.marqueeY2 = mouse.y / sc
                if (Math.abs(mouse.x - intern.startMx) >= intern.slop
                        || Math.abs(mouse.y - intern.startMy) >= intern.slop)
                    intern.moved = true
                return
            }
            if (!intern.startRects || !intern.startRects.length)
                return
            let mode = intern.handle
            if (mode === "pending") {
                if (Math.abs(mouse.x - intern.startMx) < intern.slop
                        && Math.abs(mouse.y - intern.startMy) < intern.slop)
                    return
                mode = "move"
                intern.handle = "move"
            }
            const dx = (mouse.x - intern.startMx) / sc
            const dy = (mouse.y - intern.startMy) / sc
            intern.moved = true
            const next = tools.geometryFromDragMany(mode, intern.startRects, dx, dy)
            const exclude = []
            for (let i = 0; i < intern.startRects.length; ++i)
                exclude.push(intern.startRects[i].id)
            const snapped = tools.snapGeometryMany(
                        next, mode, exclude,
                        !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
            intern.snapV = snapped.vGuide
            intern.snapH = snapped.hGuide
            const rects = snapped.rects || next
            intern.dragId = intern.dragId
            root.publishPreview(rects)
        }
        onReleased: root.finish()
        onCanceled: root.finish()
        onDoubleClicked: function (mouse) {
            const sc = root.stageScale()
            const hitId = tools.hitObject(mouse.x / sc, mouse.y / sc)
            if (!hitId.length)
                return
            const r = root.rectOf(hitId)
            if (!r)
                return
            const inset = 10  // the chrome: a double-click on a handle is not one on the box
            if (inset > 0) {
                const sx = r.x * sc
                const sy = r.y * sc
                const sw = r.w * sc
                const sh = r.h * sc
                if (mouse.x - sx < inset || sx + sw - mouse.x < inset
                        || mouse.y - sy < inset || sy + sh - mouse.y < inset)
                    return
            }
            root.doubleClicked(hitId)
        }
    }

    Rectangle {
        visible: intern.handle === "marquee" && intern.moved
        x: Math.min(intern.marqueeX1, intern.marqueeX2) * root.stageScale()
        y: Math.min(intern.marqueeY1, intern.marqueeY2) * root.stageScale()
        width: Math.abs(intern.marqueeX2 - intern.marqueeX1) * root.stageScale()
        height: Math.abs(intern.marqueeY2 - intern.marqueeY1) * root.stageScale()
        z: 6
        enabled: false
        color: Style.canvasMarqueeFill
        border.color: Style.canvasGuide
        border.width: 1
    }

    Repeater {
        model: {
            if (intern.handle === "marquee" || root.editingId.length)
                return []
            const ids = root.effectiveSelectedIds()
            if (ids.length < 2)
                return []
            const u = root.unionHandleRect()
            if (!u)
                return []
            const sc = root.stageScale()
            const box = {
                x: u.x * sc, y: u.y * sc, w: u.w * sc, h: u.h * sc
            }
            return [
                { dx: 0, dy: 0, box: box },
                { dx: 0.5, dy: 0, box: box },
                { dx: 1, dy: 0, box: box },
                { dx: 1, dy: 0.5, box: box },
                { dx: 1, dy: 1, box: box },
                { dx: 0.5, dy: 1, box: box },
                { dx: 0, dy: 1, box: box },
                { dx: 0, dy: 0.5, box: box }
            ]
        }
        delegate: Rectangle {
            required property var modelData
            width: 7
            height: 7
            z: 8
            enabled: false
            color: Style.handleFill
            border.color: Style.handleEdge
            border.width: 1
            x: modelData.box.x + modelData.dx * modelData.box.w - width / 2
            y: modelData.box.y + modelData.dy * modelData.box.h - height / 2
        }
    }

    Rectangle {
        // A guide past the canvas (a sibling's overhanging edge) stays hidden;
        // one exactly on the far edge is pulled inside the clipped stage.
        visible: intern.snapV >= 0 && intern.snapV <= root.canvasWidth
        x: Math.min(intern.snapV * root.stageScale(), root.width - width)
        y: 0
        width: 1
        height: root.height
        z: 5
        enabled: false
        color: Style.canvasGuide
        opacity: 0.85
    }
    Rectangle {
        visible: intern.snapH >= 0 && intern.snapH <= root.canvasHeight
        x: 0
        y: Math.min(intern.snapH * root.stageScale(), root.height - height)
        width: root.width
        height: 1
        z: 5
        enabled: false
        color: Style.canvasGuide
        opacity: 0.85
    }
}
