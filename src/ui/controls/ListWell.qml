import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A dark well: one outline and one dark face around its columns - the
// WellPanes declared inside, laid out left to right. One pane is a plain
// well; two or more split it, the way Libraries & Profiles and the song
// editor's Verses and verse editor share one well. Give the panes their
// widths with Layout.* as in any row, and ``ruleLeft`` to every pane after
// the first.
Rectangle {
    id: well
    default property alias panes: row.data

    radius: Style.radiusLarge
    color: Style.wellBg
    border.color: Style.wellEdge
    clip: true

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
    }
}
