pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import SeventhSlide

// Topics & Comments page of the song editor (library songs only): the words
// that group the song (Christmas, Communion) and the operator's notes on it.
// The song is SongEditState (`dialog`).
ScrollPage {
    id: panel

    /// The song editor's state. Every `panel.dialog.` below is this panel's
    /// interface to it.
    required property SongEditState dialog

    PageHeader {
        width: panel.columnWidth
        title: qsTr("Topics & Comments")
        text: qsTr("Topics group songs by season or subject, like Christmas or Thanksgiving, so "
                   + "you can find them in the library. Comments are notes for whoever uses "
                   + "this song next.")
    }

    GroupHeading {
        text: qsTr("Topics")
    }
    TagField {
        width: panel.columnWidth
        tags: panel.dialog.topics
        placeholder: qsTr("Add a topic…")
        onAdded: function (text) { panel.dialog.addTopic(text) }
        onRemoved: function (index) { panel.dialog.removeTopic(index) }
    }

    GroupHeading {
        text: qsTr("Comments")
    }
    Rectangle {
        width: panel.columnWidth
        height: 150
        radius: Style.radiusSmall
        color: comments.activeFocus ? Style.fieldFaceFocus : Style.fieldFace
        border.color: comments.activeFocus ? Style.focusEdge
                    : (commentsHover.containsMouse ? Style.controlEdgeHover : Style.controlEdge)

        MouseArea {
            id: commentsHover
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.IBeamCursor
            onClicked: comments.forceActiveFocus()
        }

        Flickable {
            id: commentsFlick
            anchors.fill: parent
            anchors.margins: 1
            contentWidth: width - Style.scrollGutter(commentsFlick)
            contentHeight: comments.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.VerticalFlick
            ScrollBar.vertical: AppScrollBar {}

            TextEdit {
                id: comments
                width: commentsFlick.width - Style.scrollGutter(commentsFlick)
                leftPadding: 9
                rightPadding: 9
                topPadding: 7
                bottomPadding: 7
                text: panel.dialog.commentsText
                color: Style.text
                font.pixelSize: Style.fontBody
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                onTextChanged: {
                    if (text !== panel.dialog.commentsText)
                        panel.dialog.commentsText = text
                }
                onCursorRectangleChanged: {
                    const r = comments.cursorRectangle
                    if (r.y < commentsFlick.contentY)
                        commentsFlick.contentY = r.y
                    else if (r.y + r.height > commentsFlick.contentY + commentsFlick.height)
                        commentsFlick.contentY = r.y + r.height - commentsFlick.height
                                                 + comments.bottomPadding
                }
                Keys.onEscapePressed: function (event) {
                    comments.focus = false
                    event.accepted = true
                }

                Text {
                    x: comments.leftPadding
                    y: comments.topPadding
                    visible: !comments.text.length && !comments.activeFocus
                    text: qsTr("Notes about this song")
                    color: Style.fieldPlaceholder
                    font.pixelSize: Style.fontBody
                }
            }
        }
    }
}
