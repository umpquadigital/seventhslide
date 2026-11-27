pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// Shared designer overlay: selection chrome, 8 resize handles, and the words
// of a box QML draws itself (a Freeform text box). A theme's stage draws no
// words here: its raster paints every element at its own place in the stack.
// The box being typed into is the stage's own TextEdit, beside these boxes.
Item {
    id: root

    property bool selected: false
    property bool editing: false
    property bool resizable: false
    property bool showContent: false
    property bool outlineWhenUnselected: false
    property color outlineColor: Style.lyricsOutline
    property string contentHtml: ""
    /// Over a selected stand-in (a theme's media area, a picture not chosen
    /// yet): what kind of box it is, as chrome.
    property color selectedTint: "transparent"
    // The object's type as it paints (each editor hands over C++'s values).
    required property color fontColor
    required property string fontFamily
    required property real fontPixelSize
    property bool fontBold: false
    property bool fontItalic: false
    property bool fontUnderline: false
    required property real lineHeight
    required property string textAlign
    required property string vAlign
    property int wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    property int contentMargins: 4
    /// Fades only what stands in for the object's pixels. Selection chrome and
    /// resize handles stay opaque: the operator has to see and grab a faint
    /// object as easily as a solid one.
    property real contentOpacity: 1.0

    readonly property bool hasText: contentHtml.length > 0
    readonly property bool showOutline: selected || editing
                                        || (outlineWhenUnselected && showContent)

    visible: selected || showContent || editing
    clip: false

    Rectangle {
        anchors.fill: parent
        visible: root.showOutline
        z: 0
        color: root.selected
               ? Qt.rgba(root.outlineColor.r, root.outlineColor.g, root.outlineColor.b, 0.08)
               : "transparent"
        border.width: root.editing ? 2 : 1
        border.color: root.editing ? Style.canvasGuide : root.outlineColor
        radius: 2
    }

    Rectangle {
        anchors.fill: parent
        visible: root.selectedTint.a > 0 && root.selected
        z: 0
        color: root.selectedTint
    }

    // Clip the words the way the output painter clips to the box.
    // Resize handles stay outside this item so they remain grabable.
    Item {
        anchors.fill: parent
        anchors.margins: root.contentMargins
        z: 1
        clip: true
        visible: root.showContent && !root.editing && root.hasText
        opacity: root.contentOpacity

        // As tall as its words and moved to where they sit: a TextEdit's own
        // vertical alignment lays the words out anew but leaves them drawn
        // where they were.
        TextEdit {
            id: boxText
            anchors.left: parent.left
            anchors.right: parent.right
            height: boxText.contentHeight
            y: {
                if (root.vAlign === "top")
                    return 0
                if (root.vAlign === "bottom")
                    return parent.height - boxText.height
                return (parent.height - boxText.height) / 2
            }
            readOnly: true
            enabled: false
            selectByMouse: false
            persistentSelection: false
            activeFocusOnPress: false
            cursorVisible: false
            padding: 0
            textFormat: TextEdit.RichText
            text: root.contentHtml
            color: root.fontColor
            font.family: root.fontFamily
            font.pixelSize: Math.max(8, Math.round(root.fontPixelSize))
            font.bold: root.fontBold
            font.italic: root.fontItalic
            font.underline: root.fontUnderline
            horizontalAlignment: {
                if (root.textAlign === "left")
                    return TextEdit.AlignLeft
                if (root.textAlign === "right")
                    return TextEdit.AlignRight
                return TextEdit.AlignHCenter
            }
            wrapMode: root.wrapMode
            property bool applyingLayout: false
            function applyCssLineHeight() {
                if (boxText.applyingLayout)
                    return
                boxText.applyingLayout = true
                TextEditTools.setLineHeight(
                            boxText, root.lineHeight > 0 ? root.lineHeight : 1.15, 0)
                boxText.applyingLayout = false
            }
            onTextChanged: Qt.callLater(boxText.applyCssLineHeight)
            Component.onCompleted: Qt.callLater(boxText.applyCssLineHeight)
            Connections {
                target: root
                function onLineHeightChanged() { Qt.callLater(boxText.applyCssLineHeight) }
                function onFontPixelSizeChanged() { Qt.callLater(boxText.applyCssLineHeight) }
            }
        }
    }

    Repeater {
        model: root.resizable && root.selected && !root.editing ? [
            { mode: "nw", dx: 0, dy: 0 },
            { mode: "n",  dx: 0.5, dy: 0 },
            { mode: "ne", dx: 1, dy: 0 },
            { mode: "e",  dx: 1, dy: 0.5 },
            { mode: "se", dx: 1, dy: 1 },
            { mode: "s",  dx: 0.5, dy: 1 },
            { mode: "sw", dx: 0, dy: 1 },
            { mode: "w",  dx: 0, dy: 0.5 }
        ] : []
        delegate: Rectangle {
            required property var modelData
            width: 7
            height: 7
            z: 8
            color: Style.handleFill
            border.color: Style.handleEdge
            border.width: 1
            x: modelData.dx * root.width - width / 2
            y: modelData.dy * root.height - height / 2
        }
    }
}
