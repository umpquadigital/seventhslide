pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect

// How one MediaBrowser entry, a list row or an icon tile, takes the mouse:
// a press selects (Ctrl / Shift add to it) and can start a drag or a
// marquee, a double-click opens or plays it.
MouseArea {
    id: area
    /// Untyped, since MediaBrowser makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var browser // MediaBrowser
    // The entry, as its view's model gives it.
    required property string kind
    required property var entryId
    required property string filename
    required property string name
    required property int index
    /// What takes the keyboard back after a press: the list or icon view.
    required property Item focusTarget

    anchors.fill: parent
    hoverEnabled: true
    cursorShape: area.browser.dragging ? Qt.ClosedHandCursor : Qt.PointingHandCursor
    acceptedButtons: Qt.LeftButton
    preventStealing: true
    onPressed: function (mouse) {
        area.browser.interacted()
        area.browser.dragPressX = mouse.x
        area.browser.dragPressY = mouse.y
        area.browser.dragPressMs = Date.now()
        area.browser.dragging = false
        const mode = area.browser.applyEntryPress(area.kind, area.entryId,
                                          area.filename, area.index,
                                          mouse.modifiers)
        if (area.kind !== "parent" && mode !== "toggle" && mode !== "range")
            area.browser.beginDragFrom(area.kind, area.entryId, area.filename)
        area.focusTarget.forceActiveFocus()
    }
    onPositionChanged: function (mouse) {
        if (!pressed || area.kind === "parent")
            return
        if (area.browser.marqueeActive) {
            area.browser.updateMarqueeFrom(area, mouse.x, mouse.y)
            return
        }
        if (!area.browser.dragging) {
            if (!MultiSelect.shouldStartDrag(mouse.x - area.browser.dragPressX,
                                             mouse.y - area.browser.dragPressY,
                                             area.browser.dragThreshold,
                                             Date.now() - area.browser.dragPressMs))
                return
        }
        if (!area.browser.pendingMove)
            return
        area.browser.entrySelection.keepOnRelease()
        area.browser.updateDragAt(area, mouse.x, mouse.y, area.name)
    }
    onDoubleClicked: function (mouse) {
        area.browser.clearDrag()
        area.browser.activateEntry(area.kind, area.entryId, area.filename,
                           !!(mouse.modifiers & Qt.ShiftModifier))
    }
    onReleased: function (mouse) {
        if (area.browser.marqueeActive) {
            area.browser.updateMarqueeFrom(area, mouse.x, mouse.y)
            area.browser.endMarquee()
            return
        }
        if (!area.browser.dragging)
            area.browser.collapsePressSelection()
        area.browser.finishDrag(area, mouse.x, mouse.y)
    }
    onCanceled: area.browser.clearDrag()
}
