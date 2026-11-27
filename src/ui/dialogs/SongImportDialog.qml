pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeventhSlide

// Import Songs: several files picked at once, read but not yet saved. One row
// for each song, checked to begin with, and for each file that gave none,
// saying why; the format each was read as on the right. Import saves what is
// checked; Cancel saves nothing. A song's words are for the song editor, after.
AppDialog {
    id: root

    required property SessionFacade session

    /// ``LibraryController::readSongFiles``' rows.
    property var rows: []
    /// The ``row`` of each song unchecked.
    property var unchecked: ({})

    readonly property var songs: root.rows.filter(r => r.importable)
    readonly property var chosen: root.songs.filter(r => !root.unchecked[r.row]).map(r => r.row)

    /// What importing the chosen songs came to: ``importReadSongs``' result.
    signal imported(var result)

    /// Read the files at ``paths`` and show what they hold.
    function openFor(paths) {
        root.rows = root.session.library.readSongFiles(paths)
        root.unchecked = ({})
        // None at all is an error the library has reported.
        root.open = root.rows.length > 0
    }

    function toggle(row) {
        const next = Object.assign({}, root.unchecked)
        if (next[row])
            delete next[row]
        else
            next[row] = true
        root.unchecked = next
    }

    function setAll(on) {
        const next = {}
        if (!on)
            root.songs.forEach(r => { next[r.row] = true })
        root.unchecked = next
    }

    function discardRead() {
        root.session.library.discardReadSongs()
    }

    title: qsTr("Import Songs")
    cardWidth: 470
    maxCardHeight: Math.min(500, root.height - 80)
    closeOnScrim: false
    danger: false
    acceptLabel: qsTr("Import Songs")
    acceptEnabled: root.chosen.length > 0
    onAccepted: root.imported(root.session.library.importReadSongs(root.chosen))
    // After ``accepted``, which closing comes before.
    onClosed: Qt.callLater(root.discardRead)

    Item {
        visible: root.songs.length > 0
        Layout.fillWidth: true
        Layout.preferredHeight: 20

        RowLayout {
            anchors.fill: parent
            spacing: 8

            CheckMark {
                checked: root.chosen.length === root.songs.length
                partial: root.chosen.length > 0
            }
            Text {
                text: qsTr("All songs")
                color: Style.text
                font.pixelSize: Style.fontBody
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("%1 of %2 selected").arg(root.chosen.length).arg(root.songs.length)
                color: Style.textCaption
                font.pixelSize: Style.fontSmall
            }
        }
        MouseArea {
            objectName: "songImportAll"
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: root.setAll(root.chosen.length !== root.songs.length)
        }
    }

    ListWell {
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: rowList.contentHeight + rowList.topMargin + rowList.bottomMargin + 2

        WellPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            showHeader: false

            ListView {
                id: rowList
                objectName: "songImportList"
                Layout.fillWidth: true
                Layout.fillHeight: true
                // The bar a gap in from the well's edge, as the Library's is.
                Layout.rightMargin: Style.scrollGap
                topMargin: 6
                bottomMargin: 6
                leftMargin: 6
                rightMargin: Style.scrollGutter(rowList)
                spacing: 3
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: root.rows
                ScrollBar.vertical: AppScrollBar {}

                delegate: PickRow {
                    id: songRow
                    required property var modelData
                    readonly property bool checkedRow: songRow.modelData.importable
                                                       && !root.unchecked[songRow.modelData.row]

                    title: songRow.modelData.title
                    subtitle: songRow.modelData.note
                    subtitleInk: Style.textWarning
                    dimmed: !songRow.checkedRow
                    leading: CheckMark {
                        enabled: songRow.modelData.importable
                        checked: songRow.checkedRow
                    }
                    trailing: Text {
                        text: songRow.modelData.format
                        color: Style.textCaption
                        font.pixelSize: Style.fontSmall
                        textFormat: Text.PlainText
                    }
                    onClicked: {
                        if (songRow.modelData.importable)
                            root.toggle(songRow.modelData.row)
                    }
                }
            }
        }
    }
}
