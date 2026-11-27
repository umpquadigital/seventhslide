import QtQuick
import SeventhSlide

Rectangle {
    id: root
    property var view: null
    property bool active: false
    property real contentX1: 0
    property real contentY1: 0
    property real contentX2: 0
    property real contentY2: 0

    readonly property real cx: view ? view.contentX : 0
    readonly property real cy: view ? view.contentY : 0

    visible: active && width >= 2 && height >= 2
    x: Math.min(contentX1, contentX2) - cx
    y: Math.min(contentY1, contentY2) - cy
    width: Math.abs(contentX2 - contentX1)
    height: Math.abs(contentY2 - contentY1)
    z: 80
    color: Style.marqueeFill
    border.width: 1
    border.color: Style.marqueeEdge
    radius: 1
}
