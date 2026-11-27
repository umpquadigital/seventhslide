import QtQuick
import SeventhSlide

// Something the app reports about a row - TODAY, IN USE, HIGHEST, OFF, KJV:
// small bold capitals on a quiet fill. ``tone``: "grey" (the default),
// "blue" (in use), "accent" (solid blue: a theme that is the song or Bible
// default), "ghost" (over a picture: one it could be), "warn" (missing) or
// "off" (switched off).
// (Words people type are TagChips.)
Rectangle {
    id: badge
    property string text: ""
    property string tone: "grey"
    /// Shown in capitals. Off for a name with its own casing (NIrV).
    property bool capitals: true

    implicitWidth: label.implicitWidth + 10
    implicitHeight: label.implicitHeight + 6
    radius: 2
    color: badge.tone === "blue" ? Style.badgeBlueBg
         : badge.tone === "accent" ? Style.accent
         : badge.tone === "off" ? Style.chipBg
         : badge.tone === "ghost" ? Style.badgeGhostBg
         : Style.badgeBg
    border.width: badge.tone === "ghost" ? 1 : 0
    border.color: Style.badgeGhostEdge

    Text {
        id: label
        anchors.centerIn: parent
        text: badge.capitals ? badge.text.toUpperCase() : badge.text
        color: badge.tone === "blue" ? Style.badgeBlueInk
             : badge.tone === "accent" ? Style.textOnAccent
             : badge.tone === "warn" ? Style.textWarning
             : badge.tone === "off" ? Style.textFaint
             : badge.tone === "ghost" ? Style.badgeGhostInk
             : Style.badgeInk
        font.pixelSize: Style.fontCaption
        font.bold: true
        font.letterSpacing: 0.4
    }
}
