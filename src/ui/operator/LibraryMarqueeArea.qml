import QtQuick
import SeventhSlide
import "../js/MultiSelect.js" as MultiSelect

// A library tab's empty space (Songs, Bibles): a press there clears the
// selection (or keeps it, with a toggle modifier) and draws a marquee over
// ``list``'s rows. Presses on a row or on the scroll bar go through.
MouseArea {
    id: area
    required property LibraryTab libraryTab
    required property ListView list
    required property ListSelection selection

    anchors.fill: parent
    anchors.margins: -area.libraryTab.marqueeBleed
    z: 20
    hoverEnabled: false
    acceptedButtons: Qt.LeftButton
    preventStealing: true

    onPressed: function (mouse) {
        const p = area.mapToItem(area.list, mouse.x, mouse.y)
        if (p.x > area.list.width - Style.scrollGutter(area.list)
                && p.x <= area.list.width
                && p.y >= 0 && p.y <= area.list.height) {
            mouse.accepted = false
            return
        }
        if (MultiSelect.isOverListRow(area.list, p.x, p.y)) {
            mouse.accepted = false
            return
        }
        area.libraryTab.interacted()
        const toggle = MultiSelect.isToggle(mouse.modifiers)
        if (!toggle)
            area.selection.clear()
        area.libraryTab.libDragging = false
        area.libraryTab.cancelLibAdd()
        area.selection.beginMarquee(area, mouse.x, mouse.y, toggle)
    }
    onPositionChanged: function (mouse) {
        if (!area.pressed || !area.selection.marqueeActive)
            return
        area.selection.updateMarquee(area, mouse.x, mouse.y)
    }
    onReleased: function (mouse) {
        if (area.selection.marqueeActive) {
            area.selection.updateMarquee(area, mouse.x, mouse.y)
            area.selection.endMarquee()
        }
    }
    onCanceled: area.selection.endMarquee()
    onWheel: function (wheel) { wheel.accepted = false }
}
