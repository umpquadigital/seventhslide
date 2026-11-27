import QtQuick
import SeventhSlide

// A heading inside an AppMenu, in sentence case.
Text {
    width: parent ? parent.width : implicitWidth
    leftPadding: 10
    topPadding: 6
    bottomPadding: 3
    color: Style.textFaint
    font.pixelSize: Style.fontCaption
    font.bold: true
}
