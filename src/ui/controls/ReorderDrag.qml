import QtQuick
import SeventhSlide
import "../js/Reorder.js" as Reorder

// One drag-to-reorder over a list of equal rows: the row held, the slot it
// would drop into, and what its floating ghost shows. Each row's
// ReorderMouseArea drives it; the list draws the insert line at
// ``insertBefore`` and fades row ``from`` while ``active``. The ghost's owner
// places it on ``ghostMoved``; the list's owner moves the row on ``moved``.
QtObject {
    id: drag

    /// The list's rows, and their pitch.
    property int count: 0
    property real rowHeight: Style.listRowH
    property real rowGap: 0

    /// The row pressed, or -1.
    property int from: -1
    /// The slot it would drop into (0 to count), or -1.
    property int insertBefore: -1
    /// The pointer has left the row: a drag, not a click. The ghost shows.
    property bool active: false

    /// What the ghost shows, and where the pointer holds it.
    property string label: ""
    property string icon: ""
    property int rank: 0
    property real grabX: 16
    property real grabY: 16

    /// The ghost should follow the pointer, at (x, y) in ``item``.
    signal ghostMoved(Item item, real x, real y)
    /// Row ``from`` was dropped where it lands at index ``to``.
    signal moved(int from, int to)

    function press(index) {
        drag.from = index
        drag.insertBefore = index
    }

    function start(label, icon, rank, grabX, grabY) {
        drag.active = true
        drag.label = label
        drag.icon = icon
        drag.rank = rank
        drag.grabX = grabX
        drag.grabY = grabY
    }

    /// The pointer is at ``y`` in the rows' coordinates, and at (x, y) in ``item``.
    function follow(y, item, itemX, itemY) {
        drag.insertBefore = Reorder.slotByPitch(drag.count, drag.rowHeight, drag.rowGap, y)
        drag.ghostMoved(item, itemX, itemY)
    }

    function cancel() {
        drag.from = -1
        drag.insertBefore = -1
        drag.active = false
        drag.label = ""
        drag.icon = ""
        drag.rank = 0
    }

    /// Drop: emits ``moved`` when the row lands somewhere new.
    function finish() {
        const to = Reorder.landing(drag.from, drag.insertBefore, drag.count)
        const from = drag.from
        drag.cancel()
        if (to >= 0)
            drag.moved(from, to)
    }
}
