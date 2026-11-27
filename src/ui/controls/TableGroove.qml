import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The groove between two columns of a table in a well (Settings > Bibles,
// Outputs, Stacks), over its TableHeaderBar and down each TableRow alike: a
// line of the well behind them, as the gaps between the rows are.
Rectangle {
    implicitWidth: 1
    Layout.preferredWidth: 1
    Layout.fillHeight: true
    color: Style.wellBg
}
