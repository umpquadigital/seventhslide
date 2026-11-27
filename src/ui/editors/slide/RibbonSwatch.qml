import QtQuick
import SeventhSlide

// A colour on the ribbon or the sidebar: a split button whose face is a glyph
// over a bar of the colour. Either part opens the
// app's colour picker. ``label`` is the quiet caption before it.
Row {
    id: swatch
    property string label: ""
    property alias value: picker.value
    property alias dialogTitle: picker.dialogTitle
    property alias alpha: picker.alpha
    /// What stands over the bar: a letter (the type's colour), or a glyph.
    property string letter: ""
    property string icon: "fillBucket"
    /// In the sidebar: the shorter control height.
    property bool compact: false
    signal edited(string value)
    /// The face's width, before the caret.
    readonly property int face: swatch.compact ? 30 : Style.ribbonH + 4
    readonly property int glyph: 14

    function openPicker() {
        picker.openDialog()
    }

    spacing: 6
    height: swatch.compact ? Style.btnCompactH : Style.ribbonH
    opacity: enabled ? 1 : Style.offOpacity

    Text {
        visible: swatch.label.length > 0
        anchors.verticalCenter: parent.verticalCenter
        text: swatch.label
        color: Style.textCaption
        font.pixelSize: swatch.compact ? Style.fontSmall : Style.ribbonCaption
    }
    AppButton {
        id: button
        size: swatch.compact ? "compact" : "ribbon"
        splitMenu: true
        implicitWidth: swatch.face + button._splitW
        onClicked: picker.openDialog()
        onMenuClicked: picker.openDialog()

        Column {
            anchors.verticalCenter: parent.verticalCenter
            x: 0
            width: swatch.face
            spacing: 1
            Text {
                visible: swatch.letter.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                text: swatch.letter
                color: button.ink
                font.pixelSize: swatch.compact ? Style.fontSmall : Style.ribbonFont - 1
                font.bold: true
            }
            Image {
                visible: swatch.letter.length === 0
                anchors.horizontalCenter: parent.horizontalCenter
                width: swatch.glyph
                height: swatch.glyph
                source: Icons.url(swatch.icon, button.ink, 2)
                sourceSize: Qt.size(swatch.glyph, swatch.glyph)
            }
            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: swatch.glyph
                height: 3
                radius: 1
                color: picker.previewColor
                border.color: Style.swatchRing
            }
        }
    }
    ColorPicker {
        id: picker
        visible: false
        anchorItem: button
        enabled: swatch.enabled
        onEdited: function (v) { swatch.edited(v) }
    }
}
