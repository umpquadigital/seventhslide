pragma ComponentBehavior: Bound
import QtQuick
import SeventhSlide

// A computer's picture choice, as ``IconPickerButton`` is an icon's: a button
// showing the picture and a caret, opening its initials and the glyphs
// (``Style.pictureGlyphs``), the colours (``Style.pictureColours``), and an
// image from a file.
AppButton {
    id: button
    /// ``picture_map``.
    property var picture: ({})
    /// The computer's name, for its initials.
    property string name: ""
    /// A glyph and colour picked; an empty glyph for initials.
    signal picked(string glyph, string colour)
    /// An image from a file is wanted: the caller asks which.
    signal fileAsked()

    readonly property bool hasImage: !!button.picture && !!button.picture.image
    readonly property string glyph: !!button.picture && !!button.picture.glyph ? button.picture.glyph : ""
    readonly property string colour: !!button.picture && !!button.picture.colour ? button.picture.colour
                                                                                : Style.pictureColours[0]
    /// A choice's picture in the menu, and a colour's swatch.
    readonly property int choiceSize: 20
    readonly property int swatchSize: 14

    size: "control"
    splitMenu: true
    menuOpen: menu.visible
    minWidth: Style.controlH
    onClicked: menu.visible ? menu.close() : menu.openAt()
    onMenuClicked: menu.visible ? menu.close() : menu.openAt()

    Avatar {
        x: Math.round((Style.controlH - width) / 2)
        anchors.verticalCenter: parent.verticalCenter
        size: 18
        picture: button.picture
        name: button.name
    }

    AppMenu {
        id: menu
        anchorItem: button
        alignRight: true
        minWidth: 0

        MenuHeading { text: qsTr("Picture") }
        // Each choice shows the picture it makes, so the chosen one is marked
        // with the "on" tint rather than a fill that would hide its colour.
        Grid {
            leftPadding: 4
            rightPadding: 4
            bottomPadding: 4
            columns: 6
            spacing: 3

            // Initials: no picture.
            AppButton {
                size: "standard"
                ghost: true
                toggled: !button.glyph.length && !button.hasImage
                toggledKind: "on"
                Avatar {
                    anchors.centerIn: parent
                    size: button.choiceSize
                    name: button.name
                }
                onClicked: {
                    menu.close()
                    button.picked("", "")
                }
            }
            Repeater {
                model: Style.pictureGlyphs
                delegate: AppButton {
                    id: glyphChoice
                    required property string modelData
                    size: "standard"
                    ghost: true
                    toggled: glyphChoice.modelData === button.glyph && !button.hasImage
                    toggledKind: "on"
                    Avatar {
                        anchors.centerIn: parent
                        size: button.choiceSize
                        picture: ({ glyph: glyphChoice.modelData, colour: button.colour })
                    }
                    onClicked: {
                        menu.close()
                        button.picked(glyphChoice.modelData, button.colour)
                    }
                }
            }
        }
        MenuRule {}
        Grid {
            leftPadding: 4
            rightPadding: 4
            topPadding: 1
            bottomPadding: 4
            columns: 6
            spacing: 3

            Repeater {
                model: Style.pictureColours
                delegate: AppButton {
                    id: colourChoice
                    required property string modelData
                    size: "standard"
                    ghost: true
                    toggled: button.glyph.length > 0 && !button.hasImage
                             && Qt.colorEqual(colourChoice.modelData, button.colour)
                    toggledKind: "on"
                    Rectangle {
                        anchors.centerIn: parent
                        width: button.swatchSize
                        height: button.swatchSize
                        radius: width / 2
                        color: colourChoice.modelData
                    }
                    // A colour is a glyph's: with initials or an image, the
                    // first glyph takes it.
                    onClicked: {
                        menu.close()
                        button.picked(button.glyph.length ? button.glyph : Style.pictureGlyphs[0],
                                      colourChoice.modelData)
                    }
                }
            }
        }
        MenuRule {}
        MenuItemRow {
            icon: "image"
            label: qsTr("From a File…")
            onTriggered: {
                menu.close()
                button.fileAsked()
            }
        }
    }
}
