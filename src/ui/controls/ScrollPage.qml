import QtQuick
import QtQuick.Controls
import SeventhSlide

// A window page that scrolls: what is declared inside, in one column. Filling
// a WindowShell's page area (anchors.fill), it runs on into the room round
// it on the right (to the scroll gap short of the window's edge), above and
// below, so it scrolls under the whole page and its scrollbar sits at the
// window's edge, as far in as a well's from the well's; the column keeps to
// the page area, whether the bar shows or not.
// ``bleeds: false`` keeps it to where it is put, the bar in its own gutter.
// ``stickyGutter: false`` lets the scrollbar go once the page fits again.
// ``interactive: false`` holds the page still (while a row is being dragged).
Flickable {
    id: page
    default property alias content: column.data
    /// The column's width, for rows that size to it.
    readonly property alias columnWidth: column.width
    property bool bleeds: true
    /// Whether the scrollbar's gutter stays once the page fits again, as it does
    /// for 16:9 lists (AppScrollBar). Off for a page whose window grows and
    /// shrinks with it.
    property alias stickyGutter: bar.stickyGutter

    readonly property int bleedRight: page.bleeds ? Style.windowPagePadX - Style.scrollGap : 0
    readonly property int bleedTop: page.bleeds ? Style.windowPagePadTop : 0
    readonly property int bleedBottom: page.bleeds ? Style.windowPagePadBottom : 0

    anchors.rightMargin: -page.bleedRight
    anchors.topMargin: -page.bleedTop
    anchors.bottomMargin: -page.bleedBottom

    contentWidth: width - Math.max(page.bleedRight, Style.scrollGutter(page))
    contentHeight: column.implicitHeight
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    flickableDirection: Flickable.VerticalFlick
    ScrollBar.vertical: AppScrollBar {
        id: bar
    }

    Column {
        id: column
        width: page.contentWidth
        topPadding: page.bleedTop
        bottomPadding: page.bleedBottom
    }
}
