pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// One entry of a MediaBrowser in icons view: a tile, its preview over its
// caption.
Item {
    id: tile
    /// The browser this entry belongs to: its selection, drag and actions.
    /// Untyped, since MediaBrowser makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var browser // MediaBrowser
    /// The icon grid's cell width and preview height.
    required property real cellW
    required property int previewH
    /// What takes the keyboard back after a press: the icon view.
    required property Item focusTarget
    required property string kind
    required property var entryId
    required property string name
    required property string filename
    required property int count
    required property string thumbUrl
    required property int index
    /// The preview's inset from the tile's top and sides.
    readonly property int inset: 3
    /// The caption under it: its lines, its inset within the tile's (so an
    /// ellipsis keeps clear of the border) and the space above and below it.
    readonly property int captionLines: 2
    readonly property int captionPadX: 2
    readonly property int captionGap: 3
    readonly property int captionPadBottom: 5
    width: tile.cellW
    height: tile.inset + tile.previewH + tile.captionGap
            + tile.captionLines * tile.browser.iconCaptionLineH + tile.captionPadBottom
    readonly property bool selected: tile.kind === "parent"
        ? tile.browser.selectedParent
        : (tile.kind === "folder"
           ? tile.browser.isFolderSelected(tile.entryId)
           : (tile.kind === "file"
              ? tile.browser.isEntryFileSelected(tile.filename, tile.entryId)
              : false))
    readonly property bool dropHere: (tile.kind === "folder"
            && tile.browser.dropHighlightId === tile.entryId)
        || (tile.kind === "parent" && tile.browser.dropHighlightId === "parent")

    Rectangle {
        anchors.fill: parent
        radius: Style.radiusSmall
        clip: true
        color: tile.dropHere ? Style.panelRowSelHover
             : Style.panelRowBg(tile.selected, tileHover.containsMouse)
        border.width: (tile.selected || tileHover.containsMouse || tile.dropHere) ? 1 : 0
        border.color: (tile.selected || tile.dropHere)
                      ? Style.listAccentSel
                      : (tileHover.containsMouse ? Style.listAccentHover : "transparent")

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 2
            z: 5
            visible: tile.browser.insertMarkerFor(tile.kind, tile.entryId, "before")
            color: Style.accent
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            z: 5
            visible: tile.browser.insertMarkerFor(tile.kind, tile.entryId, "after")
            color: Style.accent
        }

        Column {
            anchors.fill: parent
            anchors.leftMargin: tile.inset
            anchors.rightMargin: tile.inset
            anchors.topMargin: tile.inset
            spacing: tile.captionGap
            Item {
                width: parent.width
                height: tile.previewH
                Rectangle {
                    anchors.fill: parent
                    radius: 2
                    color: Style.thumbWell
                    clip: true

                    Image {
                        id: tileThumb
                        anchors.fill: parent
                        visible: status === Image.Ready
                        source: tile.kind === "file" ? tile.thumbUrl : ""
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: true
                        sourceSize: Qt.size(tile.cellW * 2, tile.previewH * 2)
                    }
                    // The glyph until the thumbnail is in, and in its place
                    // when there is none (sound, or a file that cannot be read).
                    Image {
                        anchors.centerIn: parent
                        visible: tileThumb.status !== Image.Ready
                        width: Math.min(28, parent.height - 6)
                        height: width
                        source: {
                            if (tile.kind === "parent")
                                return tile.browser.iconSvg("parent", Style.parentGlyph)
                            if (tile.kind === "folder")
                                return tile.browser.iconSvg("folder", Style.folderInk)
                            return tile.browser.iconSvg(tile.browser.fileGlyphKind(), Style.textCaption)
                        }
                        fillMode: Image.PreserveAspectFit
                        cache: true
                        sourceSize: Qt.size(28, 28)
                    }
                    // A file not here yet says where it is.
                    ArrivalMark {
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        anchors.margins: 4
                        maxWidth: parent.width - 8
                        arrival: tile.kind === "file" ? tile.browser.arrivalOf(tile.filename) : ({})
                    }
                }
            }
            // Up to two lines, broken at words (or inside a word too long for
            // a line), the second ending in an ellipsis when the name is cut;
            // top-aligned, so a row's names start level whether they take one
            // line or two.
            Text {
                width: parent.width
                leftPadding: tile.captionPadX
                rightPadding: tile.captionPadX
                lineHeightMode: Text.FixedHeight
                lineHeight: tile.browser.iconCaptionLineH
                horizontalAlignment: Text.AlignHCenter
                text: tile.kind === "parent" ? ".." : tile.name
                color: Style.text
                font.pixelSize: Style.fontCaption
                elide: Text.ElideRight
                maximumLineCount: tile.captionLines
                wrapMode: Text.Wrap
            }
        }

        MediaBrowserEntryArea {
            id: tileHover
            browser: tile.browser
            kind: tile.kind
            entryId: tile.entryId
            filename: tile.filename
            name: tile.name
            index: tile.index
            focusTarget: tile.focusTarget
        }
    }
}
