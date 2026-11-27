import QtQuick
import QtQuick.Layouts
import SeventhSlide

// A service section's title in a list row: the title, then a rule to the end
// of the row. The web remote draws the same (.service-divider in app.css).
RowLayout {
    id: section
    property string title: ""
    property color ink: Style.textCaption
    /// The shortest rule kept beside a long title.
    readonly property int minRule: 12

    spacing: 8

    Text {
        // Its own width while it fits; elided before the rule is squeezed out.
        Layout.preferredWidth: Math.min(implicitWidth,
                                        Math.max(0, section.width - section.spacing - section.minRule))
        text: section.title.length ? section.title : qsTr("Section")
        color: section.ink
        font.pixelSize: Style.fontCaption
        font.bold: true
        font.letterSpacing: 0.8
        elide: Text.ElideRight
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.minimumWidth: section.minRule
        Layout.preferredHeight: 1
        Layout.alignment: Qt.AlignVCenter
        color: Style.rule
    }
}
