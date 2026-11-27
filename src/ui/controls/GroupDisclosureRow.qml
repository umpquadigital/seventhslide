import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A GroupRow that shows or hides the rows after it ("Advanced"): its label,
// and a chevron pointing down while ``open``. The caller keeps ``open`` and
// turns it over in ``onClicked``.
GroupRow {
    id: row
    property string label: ""
    property bool open: false
    interactive: true

    Text {
        Layout.fillWidth: true
        text: row.label
        color: Style.text
        font.pixelSize: Style.fontBody
    }
    Image {
        Layout.preferredWidth: 10
        Layout.preferredHeight: 10
        rotation: row.open ? 0 : -90
        source: Icons.url("chevronDown", Style.chevronInk, 2.5)
        sourceSize: Qt.size(10, 10)
    }
}
