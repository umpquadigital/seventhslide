pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// The Insert tab: a new object of each kind the document takes, and the
// Fields window.
Row {
    id: tab
    required property SlideEditState dialog
    readonly property var kinds: tab.dialog.isTextTheme && tab.dialog.edit
                                 ? tab.dialog.edit.theme.insertKinds : []
    /// The palette's cells, in order. A theme has no drawing.
    readonly property var shapeCells: {
        const drawing = tab.dialog.isFreeform
        const shape = (id, icon, hint) => ({ id: "shape:" + id, icon: icon, hint: hint })
        const draw = (id, icon, hint) => ({ id: "draw:" + id, icon: icon, hint: hint,
                                            on: tab.dialog.drawTool === id })
        const cells = [
            shape("rectangle", "shapeRect", qsTr("Rectangle")),
            shape("rounded_rect", "shapeRound", qsTr("Rounded rectangle")),
            shape("ellipse", "shapeEllipse", qsTr("Ellipse")),
            shape("triangle", "shapeTriangle", qsTr("Triangle")),
            shape("right_triangle", "shapeRightTriangle", qsTr("Right triangle"))
        ]
        if (drawing)
            cells.push(draw("line", "shapeLine", qsTr("Line")))
        cells.push(shape("arrow", "shapeArrow", qsTr("Arrow")),
                   shape("star", "shapeStar", qsTr("Star")),
                   shape("rhombus", "shapeRhombus", qsTr("Rhombus")),
                   shape("polygon", "shapePolygon", qsTr("Polygon")),
                   shape("double_arrow", "shapeDoubleArrow", qsTr("Double arrow")),
                   shape("cloud", "shapeCallout", qsTr("Cloud callout")))
        if (drawing)
            cells.push(draw("curve", "shapeCurve", qsTr("Curve")),
                       draw("polyline", "pen", qsTr("Freeform")))
        return cells
    }
    spacing: 0
    height: parent ? parent.height : implicitHeight

    function kindOffered(id) {
        if (!tab.dialog.isTextTheme)
            return true
        for (let i = 0; i < tab.kinds.length; ++i) {
            if (tab.kinds[i].id === id)
                return true
        }
        return false
    }

    RibbonGroup {
        first: true
        height: parent.height
        row1: [
            RibbonButton {
                objectName: "ribbonInsertText"
                icon: "textBox"
                label: qsTr("Text box")
                onClicked: tab.dialog.insert("text")
            },
            RibbonButton {
                objectName: "ribbonInsertLyrics"
                visible: tab.dialog.isTextTheme && tab.kindOffered("lyrics")
                icon: "song"
                label: qsTr("Lyrics box")
                onClicked: tab.dialog.insert("lyrics")
            },
            RibbonButton {
                objectName: "ribbonInsertBible"
                visible: tab.dialog.isTextTheme && tab.kindOffered("bible_text")
                icon: "bible"
                label: qsTr("Bible box")
                onClicked: tab.dialog.insert("bible_text")
            },
            RibbonButton {
                objectName: "ribbonInsertImage"
                icon: "image"
                label: qsTr("Picture")
                onClicked: tab.dialog.insert("image")
            },
            RibbonButton {
                objectName: "ribbonInsertVideo"
                visible: tab.dialog.isFreeform
                icon: "video"
                label: qsTr("Video")
                onClicked: tab.dialog.insert("video")
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonInsertTable"
                visible: tab.dialog.isFreeform
                icon: "table"
                label: qsTr("Table")
                onClicked: tab.dialog.insert("table")
            },
            RibbonButton {
                objectName: "ribbonInsertIndicator"
                visible: tab.kindOffered("indicator")
                icon: "indicator"
                label: qsTr("Indicator")
                onClicked: tab.dialog.insert("indicator")
            },
            RibbonButton {
                objectName: "ribbonInsertClock"
                icon: "clock"
                label: qsTr("Clock")
                onClicked: tab.dialog.insert("clock")
            },
            RibbonButton {
                objectName: "ribbonInsertCountdown"
                visible: tab.kindOffered("countdown") && tab.dialog.isTextTheme
                icon: "timer"
                label: qsTr("Countdown")
                onClicked: tab.dialog.insert("countdown")
            }
        ]
    }
    // The shapes: one strip of glyphs, and the drawing tools at its end.
    RibbonGroup {
        height: parent.height
        tall: [
            RibbonShapes {
                objectName: "ribbonInsertShape"
                columns: tab.dialog.isFreeform ? 7 : 6
                cells: tab.shapeCells
                onClicked: function (id) {
                    const kind = id.slice(id.indexOf(":") + 1)
                    if (id.startsWith("shape:"))
                        tab.dialog.insert("shape", kind)
                    else
                        tab.dialog.startDrawing(tab.dialog.drawTool === kind ? "" : kind)
                }
            }
        ]
    }
    RibbonGroup {
        visible: tab.dialog.isTextTheme && (tab.kindOffered("video_area") || tab.kindOffered("image_area")
                                            || tab.kindOffered("web_area"))
        height: parent.height
        row1: [
            RibbonButton {
                objectName: "ribbonInsertVideoArea"
                visible: tab.kindOffered("video_area")
                icon: "video"
                label: qsTr("Video area")
                onClicked: tab.dialog.insert("video_area")
            },
            RibbonButton {
                objectName: "ribbonInsertImageArea"
                visible: tab.kindOffered("image_area")
                icon: "image"
                label: qsTr("Image area")
                onClicked: tab.dialog.insert("image_area")
            }
        ]
        row2: [
            RibbonButton {
                objectName: "ribbonInsertWebArea"
                visible: tab.kindOffered("web_area")
                icon: "globe"
                label: qsTr("Web area")
                onClicked: tab.dialog.insert("web_area")
            }
        ]
    }
    RibbonGroup {
        grow: true
        height: parent.height
        row1: [
            RibbonButton {
                objectName: "ribbonFields"
                icon: "braces"
                label: qsTr("Field")
                onClicked: tab.dialog.fieldsAsked()
            }
        ]
    }
}
