pragma ComponentBehavior: Bound

import QtQuick
import SeventhSlide

// One entry of a MediaBrowser in list view: a row, its thumbnail, name and
// count.
Item {
    id: rowWrap
    /// The browser this entry belongs to: its selection, drag and actions.
    /// Untyped, since MediaBrowser makes this type and Qt refuses the cycle;
    /// tools/panel_refs.py checks the names used on it.
    required property var browser // MediaBrowser
    required property string kind
    required property var entryId
    required property string name
    required property string filename
    required property int count
    required property string thumbUrl
    required property int index
    width: ListView.view.width - Style.scrollGutter(ListView.view)
    height: rowWrap.browser.rowHeight

    Rectangle {
        id: row
        anchors.fill: parent
        radius: Style.radiusSmall
        readonly property bool selected: rowWrap.kind === "parent"
            ? rowWrap.browser.selectedParent
            : (rowWrap.kind === "folder"
               ? rowWrap.browser.isFolderSelected(rowWrap.entryId)
               : (rowWrap.kind === "file"
                  ? rowWrap.browser.isEntryFileSelected(rowWrap.filename, rowWrap.entryId)
                  : false))
        readonly property bool dropHere: {
            const tid = rowWrap.browser.dropTargetId(rowWrap.kind, rowWrap.entryId)
            if (tid === null)
                return false
            return rowWrap.browser.dropHighlightId === tid || (tid === -1 && rowWrap.browser.dropHighlightId === "parent")
        }
        color: row.dropHere ? Style.panelRowSelHover
             : Style.panelRowBg(row.selected, rowHover.containsMouse)
        border.width: row.dropHere ? 1 : 0
        border.color: row.dropHere ? Style.listAccentSel : "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 2
            z: 5
            visible: rowWrap.browser.insertMarkerFor(rowWrap.kind, rowWrap.entryId, "before")
            color: Style.accent
        }
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            z: 5
            visible: rowWrap.browser.insertMarkerFor(rowWrap.kind, rowWrap.entryId, "after")
            color: Style.accent
        }

        ListRowAccent {
            cornerRadius: parent.radius
            selected: row.selected
            hovered: rowHover.containsMouse
        }

        // 16:9 thumb / folder / parent glyph
        Rectangle {
            id: listThumbBox
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            width: rowWrap.browser.listThumbW
            height: rowWrap.browser.listThumbH
            radius: 2
            color: Style.thumbWell
            clip: true

            Image {
                id: rowThumb
                anchors.fill: parent
                visible: status === Image.Ready
                source: rowWrap.kind === "file" ? rowWrap.thumbUrl : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                sourceSize: Qt.size(rowWrap.browser.listThumbW * 2, rowWrap.browser.listThumbH * 2)
            }
            // The glyph until the thumbnail is in, and in its place when
            // there is none (sound, or a file that cannot be read).
            Image {
                anchors.centerIn: parent
                visible: rowThumb.status !== Image.Ready
                width: Math.min(18, parent.height - 4)
                height: width
                source: {
                    if (rowWrap.kind === "parent")
                        return rowWrap.browser.iconSvg("parent", Style.parentGlyph)
                    if (rowWrap.kind === "folder")
                        return rowWrap.browser.iconSvg("folder", Style.folderInk)
                    return rowWrap.browser.iconSvg(rowWrap.browser.fileGlyphKind(), Style.textCaption)
                }
                fillMode: Image.PreserveAspectFit
                cache: true
                sourceSize: Qt.size(18, 18)
            }
        }

        // A file not here yet says where it is, at the end of its row.
        ArrivalMark {
            id: rowArrival
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            maxWidth: parent.width / 2
            arrival: rowWrap.kind === "file" ? rowWrap.browser.arrivalOf(rowWrap.filename) : ({})
        }

        Text {
            anchors.left: listThumbBox.right
            anchors.leftMargin: 8
            anchors.right: rowArrival.visible ? rowArrival.left : parent.right
            anchors.rightMargin: 10
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            verticalAlignment: Text.AlignVCenter
            text: {
                if (rowWrap.kind === "parent")
                    return ".."
                return rowWrap.name
            }
            color: Style.text
            font.pixelSize: Style.fontBody
            elide: Text.ElideRight
        }

        MediaBrowserEntryArea {
            id: rowHover
            browser: rowWrap.browser
            kind: rowWrap.kind
            entryId: rowWrap.entryId
            filename: rowWrap.filename
            name: rowWrap.name
            index: rowWrap.index
            focusTarget: rowWrap.ListView.view
        }
    }
}
