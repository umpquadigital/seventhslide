import QtQuick
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect

// A library list row under the pointer (Songs, Bibles): a press selects by the
// tab's ``selection`` rules, a drag past the threshold carries the selection
// to the service column (``dragStarted``: the tab begins it with what it
// carries), a drag that began as a marquee widens it, and a double-click is
// ``activated`` with the row selected alone: with Shift, ``live``, straight
// past a stack's Preview.
MouseArea {
    id: press
    required property LibraryTab libraryTab
    required property ListSelection selection
    /// The row's key in ``selection``.
    required property var key
    /// A drag may start (Bibles: only while a service is open).
    property bool canDrag: true
    /// The drag begins at ``x``, ``y`` here: the tab calls ``beginLibAdd``.
    signal dragStarted(real x, real y)
    signal activated(bool live)

    anchors.fill: parent
    hoverEnabled: true
    cursorShape: Qt.PointingHandCursor
    preventStealing: true

    onPressed: function (mouse) {
        press.libraryTab.interacted()
        press.libraryTab.libDragging = false
        press.libraryTab.pressX = mouse.x
        press.libraryTab.pressY = mouse.y
        press.libraryTab.pressMs = Date.now()
        press.selection.press(press.key, mouse.modifiers)
    }
    onPositionChanged: function (mouse) {
        if (!press.pressed)
            return
        if (press.selection.marqueeActive) {
            press.selection.updateMarquee(press, mouse.x, mouse.y)
            return
        }
        if (press.libraryTab.libDragging) {
            press.libraryTab.updateLibAdd(press, mouse.x, mouse.y)
            return
        }
        const tab = press.libraryTab
        if (!MultiSelect.shouldStartDrag(mouse.x - tab.pressX, mouse.y - tab.pressY,
                                         tab.dragThreshold, Date.now() - tab.pressMs))
            return
        if (!press.canDrag)
            return
        press.selection.keepOnRelease()
        press.libraryTab.libDragging = true
        press.dragStarted(mouse.x, mouse.y)
        press.libraryTab.updateLibAdd(press, mouse.x, mouse.y)
    }
    onReleased: function (mouse) {
        if (press.selection.marqueeActive) {
            press.selection.updateMarquee(press, mouse.x, mouse.y)
            press.selection.endMarquee()
        } else if (press.libraryTab.libDragging) {
            press.libraryTab.finishLibAdd(press, mouse.x, mouse.y)
        } else {
            press.selection.release()
        }
        press.libraryTab.libDragging = false
    }
    onCanceled: {
        if (press.selection.marqueeActive)
            press.selection.endMarquee()
        if (press.libraryTab.libDragging)
            press.libraryTab.cancelLibAdd()
        press.libraryTab.libDragging = false
        press.selection.keepOnRelease()
    }
    onDoubleClicked: function (mouse) {
        if (press.libraryTab.libDragging) {
            press.libraryTab.cancelLibAdd()
            press.libraryTab.libDragging = false
        }
        press.selection.selectOnly(press.key)
        press.activated(!!(mouse.modifiers & Qt.ShiftModifier))
    }
}
