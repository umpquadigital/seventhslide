pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// The frame every window shares (Settings, Output, the song editor,
// Services): a title bar with the window's name, a rail of pages on the
// left, the page itself, and a footer bar with Cancel and Save. The window's
// scrim, keys and confirms stay with the window; this is only the box. Size and centre it where the window puts it.
//
// ``pages`` are { id, label, icon, count } (icon: an Icons glyph; count,
// when given, sits at the right: Services' tags) or { heading } for a quiet
// label between groups of pages; none, no rail. A long rail scrolls.
// ``railSearchPlaceholder`` puts a search under the heading marked
// { heading, search: true }, its words in ``railSearchText``: the window
// narrows the pages after it by them (Services, its tags).
// The window owns which page shows: bind ``page`` and switch on
// ``pageActivated``. What is declared inside is the page area, inset by the
// page padding unless ``fullBleed``.
Rectangle {
    id: shell

    property string title: ""
    property var pages: []
    property string page: ""
    signal pageActivated(string id)
    /// The rail's search: its placeholder, "" for none; and what it holds.
    property string railSearchPlaceholder: ""
    property string railSearchText: ""
    /// Where the rail's search goes: after this entry of ``pages``; -1, none.
    readonly property int railSearchAt: shell.railSearchPlaceholder.length
                                        ? shell.pages.findIndex(function (p) { return !!p.search })
                                        : -1

    /// A quiet note at the footer's left (a song edited only in its service).
    property string note: ""
    /// Why Save was refused; shown in place of the note.
    property string errorText: ""
    property string acceptLabel: qsTr("Save")
    property string cancelLabel: qsTr("Cancel")
    property bool acceptEnabled: true
    /// A lone Close (a page whose changes apply as they are made); it emits
    /// ``canceled``.
    property bool closeOnly: false
    signal accepted()
    signal canceled()

    /// Save: leave the field being typed in, so it commits what it holds (a
    /// field commits as its focus leaves), then ``accepted``.
    function accept() {
        shell.forceActiveFocus()
        shell.accepted()
    }

    /// The page area without its padding (a well that runs to the edges).
    property bool fullBleed: false
    default property alias content: body.data
    /// The page area, for layers that place things in its coordinates.
    readonly property alias body: body

    // A page in the rail, or a heading between groups of them.
    component RailItem: Rectangle {
        id: railItem
        required property var modelData
        /// The page showing: the shell's.
        property string current: ""
        signal activated(string id)
        readonly property bool heading: modelData.heading !== undefined
        readonly property bool on: !heading && modelData.id === railItem.current
        readonly property bool counted: !heading && modelData.count !== undefined
        width: parent ? parent.width : 0
        height: heading ? 30 : 32
        radius: Style.radiusSmall
        color: railItem.on ? Style.railItemOn
             : (railHover.containsMouse && !railItem.heading ? Style.ghostHover
                                                             : "transparent")

        Rectangle {
            visible: railItem.on
            width: 3
            height: parent.height
            radius: 1
            color: Style.selectedBar
        }
        // A heading sits at the rail's edge, as a label over a field does:
        // over the search under it, and the pages' own edges.
        Text {
            visible: railItem.heading
            x: 1
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            text: railItem.heading ? railItem.modelData.heading : ""
            color: Style.textFaint
            font.pixelSize: Style.fontSmall
            font.bold: true
        }
        Row {
            visible: !railItem.heading
            x: 10
            width: parent.width - 10 - (countLabel.visible ? countLabel.width + 14 : 8)
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Image {
                id: railIcon
                anchors.verticalCenter: parent.verticalCenter
                width: 16
                height: 16
                source: railItem.heading ? "" : Icons.url(railItem.modelData.icon,
                                  railItem.on ? Style.textBright
                                  : (railHover.containsMouse ? Style.textHover
                                                             : Style.textCaption), 2)
                sourceSize: Qt.size(16, 16)
            }
            Text {
                width: parent.width - railIcon.width - parent.spacing
                anchors.verticalCenter: parent.verticalCenter
                text: railItem.heading ? "" : railItem.modelData.label
                color: railItem.on ? Style.textStrong
                     : (railHover.containsMouse ? Style.textHover : Style.textCaption)
                font.pixelSize: Style.fontUi
                font.weight: railItem.on ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight
            }
        }
        Text {
            id: countLabel
            visible: railItem.counted
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: railItem.counted ? String(railItem.modelData.count) : ""
            color: Style.textFaint
            font.pixelSize: Style.fontSmall
        }
        MouseArea {
            id: railHover
            anchors.fill: parent
            enabled: !railItem.heading
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: if (!railItem.on) railItem.activated(railItem.modelData.id)
        }
    }

    radius: Style.radiusLarge
    color: Style.panelBg

    // A click on the window's own surface goes no further (not to the scrim).
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: function (wheel) { wheel.accepted = true }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        Rectangle {
            id: titleBar
            Layout.fillWidth: true
            Layout.preferredHeight: Style.windowBarH
            color: Style.windowBar
            topLeftRadius: Style.radiusLarge - 1
            topRightRadius: Style.radiusLarge - 1

            Text {
                x: 16
                anchors.verticalCenter: parent.verticalCenter
                text: shell.title
                color: Style.textBright
                font.pixelSize: Style.fontHeading
                font.bold: true
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Style.border
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                id: rail
                visible: shell.pages.length > 0
                Layout.preferredWidth: Style.windowRailW
                Layout.fillHeight: true
                color: Style.windowRail

                Flickable {
                    id: railFlick
                    anchors.fill: parent
                    // Scrolling, the bar a gap in from the rail's edge (and
                    // its rule), as a well's is from the well's.
                    anchors.rightMargin: railFlick.interactive ? 1 + Style.scrollGap : 0
                    contentHeight: railColumn.height + 20
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentHeight > height
                    ScrollBar.vertical: AppScrollBar {}

                Column {
                    id: railColumn
                    x: 8
                    y: 10
                    // The scrollbar's room only when the rail scrolls.
                    width: railFlick.interactive ? railFlick.width - 8 - Style.scrollGutter(railFlick)
                                                 : railFlick.width - 16
                    spacing: 2

                    Repeater {
                        model: shell.railSearchAt >= 0 ? shell.pages.slice(0, shell.railSearchAt + 1)
                                                       : shell.pages
                        delegate: RailItem {
                            current: shell.page
                            onActivated: function (id) { shell.pageActivated(id) }
                        }
                    }
                    // The rail's search, close under its heading and a little
                    // over the pages it narrows; outside them, so narrowing
                    // them leaves it as it is, typing.
                    Item {
                        visible: shell.railSearchAt >= 0
                        width: railColumn.width
                        height: Style.controlH + 4
                        SearchField {
                            id: railSearch
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            placeholder: shell.railSearchPlaceholder
                            text: shell.railSearchText
                            onTextChanged: shell.railSearchText = text
                            // Escape empties it, then leaves it.
                            onEscaped: {
                                if (shell.railSearchText.length)
                                    shell.railSearchText = ""
                                else
                                    railSearch.blur()
                            }
                        }
                    }
                    Repeater {
                        model: shell.railSearchAt >= 0 ? shell.pages.slice(shell.railSearchAt + 1) : []
                        delegate: RailItem {
                            current: shell.page
                            onActivated: function (id) { shell.pageActivated(id) }
                        }
                    }
                }
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 1
                    height: parent.height
                    color: Style.border
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Item {
                    id: body
                    anchors.fill: parent
                    anchors.leftMargin: shell.fullBleed ? 0 : Style.windowPagePadX
                    anchors.rightMargin: shell.fullBleed ? 0 : Style.windowPagePadX
                    anchors.topMargin: shell.fullBleed ? 0 : Style.windowPagePadTop
                    anchors.bottomMargin: shell.fullBleed ? 0 : Style.windowPagePadBottom
                }
            }
        }

        Rectangle {
            id: footerBar
            Layout.fillWidth: true
            Layout.preferredHeight: Style.windowBarH
            color: Style.windowBar
            bottomLeftRadius: Style.radiusLarge - 1
            bottomRightRadius: Style.radiusLarge - 1

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                color: Style.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 14
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: shell.errorText.length ? shell.errorText : shell.note
                    color: shell.errorText.length ? Style.textDanger : Style.textHint
                    font.pixelSize: Style.fontBody
                    elide: Text.ElideRight
                }
                AppButton {
                    visible: !shell.closeOnly
                    label: shell.cancelLabel
                    minWidth: 76
                    onClicked: shell.canceled()
                }
                AppButton {
                    visible: !shell.closeOnly
                    label: shell.acceptLabel
                    primary: true
                    minWidth: 76
                    enabled: shell.acceptEnabled
                    onClicked: shell.accept()
                }
                AppButton {
                    visible: shell.closeOnly
                    label: qsTr("Close")
                    minWidth: 76
                    onClicked: shell.canceled()
                }
            }
        }
    }

    // The outline, over everything, so full-bleed content cannot hide it.
    Rectangle {
        anchors.fill: parent
        radius: shell.radius
        color: "transparent"
        border.color: Style.windowEdge
        z: 1000
        enabled: false
    }
}
