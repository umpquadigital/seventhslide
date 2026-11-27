import QtQuick
import SeventhSlide

// Asked when a window with unsaved changes is closed (Cancel, Escape, a click
// outside it): ``accepted`` lets them go.
AppDialog {
    title: qsTr("Discard your changes?")
    blurb: qsTr("Closing the editor now loses what you have not saved.")
    acceptLabel: qsTr("Discard")
}
