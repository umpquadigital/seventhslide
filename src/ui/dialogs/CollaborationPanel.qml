pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeventhSlide

// The Collaboration window's page: the open library's shared log
// (``CollaborationLog``) in a dark well, as the other windows' lists are: the
// messages the computers that sync it leave each other and a note of what each
// changed, newest at the foot. Under the well the field to send a message
// from. A note, a row as the
// wells' rows are, opens to the records it names. A viewer reads it and sends
// nothing. Its frame and title are CollaborationWindow's.
//
// Every row keeps to one grid: pictures, carets and record glyphs are centred
// in the picture column, and every line of text starts at ``textX``.
Item {
    id: root
    required property SyncController sync
    /// A message could not be sent: why.
    signal failed(string message)

    readonly property CollaborationLog log: root.sync ? root.sync.log : null
    /// The message being written.
    property string draft: ""
    /// The message last sent, for the field again if it turns out not to go.
    property string sent: ""

    // What is being written is for this library's computers.
    readonly property string library: root.log ? root.log.library : ""
    onLibraryChanged: {
        root.draft = ""
        root.sent = ""
    }

    // The grid. ``edge`` is the well's inset, as Settings' tables' rows are;
    // ``inset`` is a row's own, before its picture column.
    readonly property int edge: 6
    readonly property int inset: 10
    readonly property int pictureSize: 24
    readonly property int textX: root.inset + root.pictureSize + 8
    /// Space before a message's writer or a note; before a message that
    /// follows on from the one above.
    readonly property int entryGap: 10
    readonly property int runGap: 2
    readonly property int noteH: Style.listRowH
    readonly property int recordH: 24
    readonly property int glyphSize: 12

    /// The keys to the field, to write a message straight away.
    function focusField() {
        if (draftField.visible)
            draftField.focusInput()
    }

    /// To the newest, and stay with what comes after it.
    function toNewest() {
        entries.stickToEnd = true
        entries.positionViewAtEnd()
    }

    function send() {
        if (!root.log || root.draft.trim().length === 0)
            return
        const err = root.log.send(root.draft)
        if (err.length) {
            root.failed(err)
            return
        }
        root.sent = root.draft
        root.draft = ""
        root.toNewest()
    }

    Connections {
        target: root.log
        function onFailed(message) {
            // Back in the field to try again, unless another is being written.
            if (root.draft.length === 0)
                root.draft = root.sent
            root.failed(message)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true

            WellPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                showHeader: false

                ListView {
                    id: entries
                    objectName: "collaborationEntries"
                    /// At the newest, it stays there as more come.
                    property bool stickToEnd: true
                    /// The newest row is in full view. Not ``atYEnd``, which
                    /// counts the margin under it, and positionViewAtEnd leaves
                    /// that out.
                    readonly property bool atNewest:
                        entries.contentY + entries.height >= entries.originY + entries.contentHeight - 1

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The bar a gap in from the well's edge, as the Library's is.
                    Layout.rightMargin: Style.scrollGap
                    clip: true
                    // Rows only once shown and laid out: made narrower, their
                    // words wrapped tall for a moment, and the bar could keep
                    // the gutter that overflow reserved once the window opened.
                    model: root.visible && entries.width > 0 ? root.log : null
                    topMargin: root.edge
                    bottomMargin: root.edge
                    leftMargin: root.edge
                    rightMargin: Style.scrollGutter(entries)
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar {
                        onDraggingChanged: entries.stickToEnd = !dragging && entries.atNewest
                    }

                    onMovementEnded: entries.stickToEnd = entries.atNewest
                    onCountChanged: {
                        if (entries.stickToEnd)
                            Qt.callLater(entries.positionViewAtEnd)
                    }
                    onHeightChanged: {
                        if (entries.stickToEnd)
                            Qt.callLater(entries.positionViewAtEnd)
                    }

                    delegate: Column {
                        id: row
                        required property int index
                        required property string entryId
                        required property string kind
                        required property string name
                        required property var picture
                        required property string timeText
                        required property string text
                        required property string summary
                        required property var items
                        required property bool showDay
                        required property string day
                        required property bool showSender
                        required property bool expanded

                        width: entries.width - entries.leftMargin - Math.max(root.edge, entries.rightMargin)
                        topPadding: row.showDay ? 0
                                  : (row.kind === "message" && !row.showSender ? root.runGap : root.entryGap)

                        // The day, over its first row.
                        // The first a little in from the well's top, as the
                        // rows' words are; the rest well apart from the row
                        // above.
                        Item {
                            visible: row.showDay
                            width: parent.width
                            height: visible ? dayTitle.implicitHeight + root.entryGap
                                              + (row.index > 0 ? root.entryGap : root.inset - root.edge)
                                            : 0
                            SectionTitle {
                                id: dayTitle
                                x: root.inset
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: root.entryGap
                                width: parent.width - root.inset * 2
                                title: row.day
                            }
                        }

                        // A message: its writer's picture and name over the first of a
                        // run, then what it says.
                        Item {
                            visible: row.kind === "message"
                            width: parent.width
                            height: visible ? message.implicitHeight : 0

                            Avatar {
                                visible: row.showSender
                                x: root.inset
                                size: root.pictureSize
                                picture: row.picture
                                name: row.name
                            }
                            Column {
                                id: message
                                x: root.textX
                                width: parent.width - x - root.inset
                                spacing: 1

                                Row {
                                    visible: row.showSender
                                    spacing: 6
                                    Text {
                                        id: senderName
                                        text: row.name
                                        color: Style.logName
                                        font.pixelSize: Style.fontBody
                                        font.weight: Font.DemiBold
                                    }
                                    Text {
                                        anchors.baseline: senderName.baseline
                                        text: row.timeText
                                        color: Style.logTime
                                        font.pixelSize: Style.fontCaption
                                    }
                                }
                                TextEdit {
                                    width: parent.width
                                    text: row.text
                                    color: Style.text
                                    font.pixelSize: Style.fontBody
                                    wrapMode: TextEdit.Wrap
                                    readOnly: true
                                    selectByMouse: true
                                    selectionColor: Style.accent
                                    textFormat: TextEdit.PlainText
                                }
                            }
                        }

                        // A note of what a computer changed, a row as the wells' rows
                        // are (their fill, their bar under the pointer), opened to its
                        // records.
                        Rectangle {
                            visible: row.kind === "changes"
                            width: parent.width
                            height: visible ? note.implicitHeight : 0
                            radius: Style.radiusSmall
                            color: noteHead.containsMouse ? Style.logNoteRowHover : Style.logNoteRow

                            ListRowAccent {
                                hovered: noteHead.containsMouse
                                cornerRadius: Style.radiusSmall
                            }

                            Column {
                                id: note
                                width: parent.width
                                bottomPadding: row.expanded ? 3 : 0

                                Item {
                                    width: parent.width
                                    height: root.noteH

                                    Image {
                                        x: root.inset + (root.pictureSize - width) / 2
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: root.glyphSize
                                        height: root.glyphSize
                                        sourceSize: Qt.size(width, height)
                                        source: Icons.url(row.expanded ? "chevronDown" : "chevronRight",
                                                          Style.logNoteInk, 2.5)
                                    }
                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: root.textX
                                        anchors.right: noteTime.left
                                        anchors.rightMargin: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: row.summary
                                        color: Style.logNoteInk
                                        font.pixelSize: Style.fontSmall
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        id: noteTime
                                        anchors.right: parent.right
                                        anchors.rightMargin: root.inset
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: row.timeText
                                        color: Style.logTime
                                        font.pixelSize: Style.fontCaption
                                    }
                                    MouseArea {
                                        id: noteHead
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (root.log)
                                                root.log.setExpanded(row.entryId, !row.expanded)
                                        }
                                    }
                                }

                                Rectangle {
                                    visible: row.expanded
                                    x: 3
                                    width: parent.width - 3
                                    height: 1
                                    color: Style.groupRule
                                }

                                Repeater {
                                    model: row.expanded ? row.items : []
                                    delegate: Item {
                                        id: record
                                        required property var modelData
                                        width: note.width
                                        height: root.recordH

                                        Image {
                                            x: root.inset + (root.pictureSize - width) / 2
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: root.glyphSize
                                            height: root.glyphSize
                                            sourceSize: Qt.size(width, height)
                                            source: Icons.url(record.modelData.glyph, Style.iconIdle, 2)
                                        }
                                        Text {
                                            id: recordTitle
                                            x: root.textX
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: Math.min(implicitWidth,
                                                            parent.width - x - root.inset - 8
                                                            - Math.min(recordDetail.implicitWidth, parent.width / 3))
                                            text: record.modelData.title
                                            color: Style.text
                                            font.pixelSize: Style.fontSmall
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            id: recordDetail
                                            anchors.left: recordTitle.right
                                            anchors.leftMargin: 8
                                            anchors.right: parent.right
                                            anchors.rightMargin: root.inset
                                            anchors.verticalCenter: parent.verticalCenter
                                            horizontalAlignment: Text.AlignRight
                                            text: record.modelData.detail
                                            color: Style.textCaption
                                            font.pixelSize: Style.fontSmall
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Nothing written or changed yet.
                    EmptyListNote {
                        visible: entries.count === 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        title: qsTr("No messages yet")
                        hint: !!root.log && root.log.canPost
                              ? qsTr("Write one below. What each computer changes shows here too.")
                              : qsTr("What each computer says and changes shows here.")
                    }
                }
            }
        }

        // The field to send from, under the well; a viewer's says why there
        // is none.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: -4
            spacing: 6
            visible: !!root.log && root.log.canPost

            DialogField {
                id: draftField
                Layout.fillWidth: true
                text: root.draft
                placeholder: qsTr("Message the other computers")
                onEdited: value => root.draft = value
                onSubmitted: root.send()
            }
            AppButton {
                size: "control"
                primary: true
                label: qsTr("Send")
                enabled: root.draft.trim().length > 0
                onClicked: root.send()
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: -4
            Layout.preferredHeight: Style.controlH
            verticalAlignment: Text.AlignVCenter
            visible: !!root.log && root.log.available && !root.log.canPost
            text: qsTr("This computer only views this library, so it reads messages but cannot send them.")
            color: Style.textHint
            font.pixelSize: Style.fontBody
            elide: Text.ElideRight
        }
    }
}
