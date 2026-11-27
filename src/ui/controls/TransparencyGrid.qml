import QtQuick

// Display-sized transparency checkerboard. Cell size is in item pixels so the
// grid stays stable while a designer draft image changes resolution (drag).
Image {
    id: root

    property int cellPx: 24

    fillMode: Image.Stretch
    asynchronous: false
    cache: false
    smooth: false
    mipmap: false
    sourceSize: Qt.size(Math.max(2, Math.round(width)), Math.max(2, Math.round(height)))
    source: (width > 2 && height > 2)
            ? ("image://themepreview/grid?cell=" + Math.max(4, cellPx))
            : ""
}
