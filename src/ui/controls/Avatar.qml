import QtQuick
import SeventhSlide

// A computer's picture, round (``picture_map``): its image, a glyph on a tint
// of its colour, or the initials of its ``name``.
Item {
    id: avatar
    /// ``{image}``, ``{glyph, colour}``, or empty.
    property var picture: ({})
    property string name: ""
    property int size: 24

    readonly property string image: !!avatar.picture && !!avatar.picture.image ? avatar.picture.image : ""
    readonly property string glyph: !!avatar.picture && !!avatar.picture.glyph ? avatar.picture.glyph : ""
    readonly property color colour: !!avatar.picture && !!avatar.picture.colour ? avatar.picture.colour
                                                                               : Style.pictureInitialsInk

    /// "JM" for "John's MacBook": the first letters of the first two words.
    function initials(name) {
        const words = name.split(/\s+/).filter(w => w.length > 0)
        return words.slice(0, 2).map(w => w.charAt(0)).join("").toUpperCase()
    }

    implicitWidth: avatar.size
    implicitHeight: avatar.size

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: avatar.image.length === 0
        color: avatar.glyph.length
               ? Qt.rgba(avatar.colour.r, avatar.colour.g, avatar.colour.b, Style.pictureTint)
               : Style.pictureInitialsBg
    }
    Image {
        anchors.centerIn: parent
        visible: avatar.glyph.length > 0 && avatar.image.length === 0
        // About half the disc, and even when it is (odd when it is odd), so
        // the margins around it are whole pixels.
        width: avatar.size - 2 * Math.floor(avatar.size * 0.225)
        height: width
        sourceSize: Qt.size(width, height)
        source: avatar.glyph.length ? Icons.url(avatar.glyph, avatar.colour, 2) : ""
    }
    BadgeText {
        anchors.centerIn: parent
        visible: avatar.glyph.length === 0 && avatar.image.length === 0
        text: avatar.initials(avatar.name)
        color: Style.pictureInitialsInk
        font.pixelSize: Math.max(Style.fontMicro - 1, Math.round(avatar.size * 0.4))
        font.bold: true
    }
    // An image is round already (``setComputerPictureFile``).
    Image {
        anchors.fill: parent
        visible: avatar.image.length > 0
        source: avatar.image.length ? "data:image/png;base64," + avatar.image : ""
        sourceSize: Qt.size(avatar.size * 2, avatar.size * 2)
        smooth: true
        mipmap: true
        fillMode: Image.PreserveAspectFit
    }
}
