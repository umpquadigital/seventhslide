import QtQuick
import QtQuick.Layouts
import SeventhSlide

// One translation in Settings > Bibles' table: its name, its abbreviation and
// a switch that offers it for lookup, in the header's columns
// (``abbreviationW``, ``onW``, padding and all); off, its words fade. A double
// click on the name or the abbreviation asks to edit that cell
// (``editAsked``); the list answers with ``editing``, and the cell becomes a
// field: Enter or clicking away keeps it (``edited``), Escape drops it
// (``editCanceled``).
TableRow {
    id: row

    property string name: ""
    property string abbreviation: ""
    property bool on: true
    /// The cell being edited: "", "name" or "abbreviation".
    property string editing: ""
    property int abbreviationW: 116
    property int onW: 56
    signal editAsked(string field)
    signal edited(string field, string text)
    signal editCanceled()
    signal toggled()

    onDoubleClicked: function (x) {
        row.editAsked(x >= abbreviationCell.x ? "abbreviation" : "name")
    }

    TableTextCell {
        Layout.fillWidth: true
        text: row.name
        open: row.editing === "name"
        dim: !row.on
        bold: row.selected
        onCommitted: function (text) { row.edited("name", text) }
        onCanceled: row.editCanceled()
    }
    TableGroove {}
    TableTextCell {
        id: abbreviationCell
        Layout.preferredWidth: row.abbreviationW
        text: row.abbreviation
        open: row.editing === "abbreviation"
        dim: !row.on
        maxLength: 12
        ink: Style.textSoft
        onCommitted: function (text) { row.edited("abbreviation", text) }
        onCanceled: row.editCanceled()
    }
    TableGroove {}
    Item {
        Layout.preferredWidth: row.onW
        Layout.fillHeight: true
        ToggleSwitch {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            checked: row.on
            onToggled: row.toggled()
        }
    }
}
