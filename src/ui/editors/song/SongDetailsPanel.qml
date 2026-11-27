pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// Details page of the song editor (library songs only): the authors, a table
// in a dark well laid out as Settings > Bibles and Stacks are (Name and
// Credit; Remove and Add over it; a double click on a name edits it in place,
// the credits are picked in their cell); then the songbook, and the
// copyright, CCLI number and key. The song is SongEditState (`dialog`).
ScrollPage {
    id: panel

    /// The song editor's state. Every `panel.dialog.` below is this panel's
    /// interface to it.
    required property SongEditState dialog
    /// The author whose name is open for editing, or -1.
    property int editingIndex: -1
    /// The Credit column, header and rows alike, padding and all.
    readonly property int creditW: 220

    /// The credits an author can be given, for the checklist: the usual ones
    /// and any free-text credit the author already has.
    function creditChoices(credits) {
        const out = panel.dialog.authorRoleOptions.map(function (o) {
            return { id: o.key, label: o.label }
        })
        for (const c of credits || []) {
            if (c.type === "other" && String(c.label || "").length)
                out.push({ id: "other:" + c.label, label: String(c.label) })
        }
        return out
    }

    /// The checklist ids of ``credits``.
    function creditIds(credits) {
        return (credits || []).map(function (c) {
            return c.type === "other" ? "other:" + c.label : String(c.type)
        })
    }

    /// Open author ``index``'s name.
    function beginEdit(index) {
        panel.dialog.selectAuthor(index)
        panel.editingIndex = index
    }

    /// Author ``index``'s name closes with ``text``. The model notifies a
    /// rename in place, so it is applied at once (Bibles and Stacks wait a
    /// moment because their lists rebuild).
    function endEdit(index, text) {
        if (panel.editingIndex !== index)
            return
        panel.editingIndex = -1
        panel.dialog.finishAuthorName(index, text)
    }

    // A new author opens with the cursor in its name.
    Connections {
        target: panel.dialog
        function onAuthorAdded(index) {
            panel.editingIndex = index
            authorList.positionViewAtIndex(index, ListView.End)
        }
    }
    // A name open for editing follows its author when rows above go, and
    // closes with its author.
    Connections {
        target: panel.dialog.authorModel
        function onRowsRemoved(parent, first, last) {
            if (panel.editingIndex > last)
                panel.editingIndex -= last - first + 1
            else if (panel.editingIndex >= first)
                panel.editingIndex = -1
        }
    }

    PageHeader {
        width: panel.columnWidth
        title: qsTr("Details")
        text: qsTr("Who wrote the song, the copyright line and CCLI number that can show on "
                   + "screen, and its songbook number and key.")
    }

    // Remove for the author picked, and Add, over the well.
    GroupHeading {
        text: qsTr("Authors")
        tools: [
            AppButton {
                size: "control"
                iconSource: Icons.url("trash", ink)
                enabled: panel.dialog.hasAuthorSelection
                onClicked: panel.dialog.removeAuthor(panel.dialog.selectedAuthorIndex)
            },
            AppButton {
                size: "control"
                primary: true
                label: qsTr("Add")
                iconSource: Icons.url("plus", ink)
                onClicked: panel.dialog.addAuthor()
            }
        ]
    }
    ListWell {
        id: authorWell
        width: panel.columnWidth
        // On a page that scrolls the well cannot take the height left, so it
        // grows with the authors: two rows' worth (the empty words fit) to
        // five, then the table scrolls.
        readonly property int shownRows: Math.max(2, Math.min(panel.dialog.authorModel.count, 5))
        height: 2 + 30 + 12 + authorWell.shownRows * Style.groupRowH
                + (authorWell.shownRows - 1) * 3

        WellPane {
            Layout.fillWidth: true
            Layout.fillHeight: true
            // The columns' names are the well's bar.
            showHeader: false

            TableHeaderBar {
                rightInset: Style.scrollGutter(authorList)

                TableColumnHead {
                    Layout.fillWidth: true
                    label: qsTr("Name")
                }
                TableGroove {}
                TableColumnHead {
                    Layout.preferredWidth: panel.creditW + 6
                    label: qsTr("Credit")
                }
            }

            ListView {
                id: authorList
                Layout.fillWidth: true
                Layout.fillHeight: true
                // The bar a gap in from the well's edge, as the Library's is.
                Layout.rightMargin: Style.scrollGap
                // Inset and apart, as the settings tables' rows are.
                topMargin: 6
                bottomMargin: 6
                leftMargin: 6
                rightMargin: Style.scrollGutter(authorList)
                spacing: 3
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                // The page scrolls past a table that fits.
                interactive: contentHeight > height
                // No current item of the view's own: it would build a second
                // row for a new author, whose name cell would take the
                // keyboard from the first.
                currentIndex: -1
                model: panel.dialog.authorModel
                ScrollBar.vertical: AppScrollBar {}

                delegate: TableRow {
                    id: author
                    required property int index
                    required property string name
                    required property var credits

                    implicitHeight: Style.groupRowH
                    selected: panel.dialog.selectedAuthorIndex === author.index
                    onClicked: panel.dialog.selectAuthor(author.index)
                    onDoubleClicked: function (x) {
                        if (x < nameCell.width)
                            panel.beginEdit(author.index)
                    }

                    // A person, then the name.
                    TableTextCell {
                        id: nameCell
                        Layout.fillWidth: true
                        leftInset: 6 + Style.tileSize + 10
                        text: author.name
                        open: panel.editingIndex === author.index
                        escapeKeeps: true
                        ink: Style.textBright
                        bold: true
                        onCommitted: function (text) { panel.endEdit(author.index, text) }

                        HandleTile {
                            anchors.left: parent.left
                            anchors.leftMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            iconSource: Icons.url("person", Style.textSoft, 2)
                        }
                    }
                    TableGroove {}
                    Item {
                        Layout.preferredWidth: panel.creditW
                        Layout.fillHeight: true
                        ChecklistField {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            noneText: qsTr("Not credited")
                            manyText: qsTr("%1 credits")
                            actionLabel: qsTr("Other…")
                            model: panel.creditChoices(author.credits)
                            checkedIds: panel.creditIds(author.credits)
                            onToggled: function (id) {
                                panel.dialog.selectAuthor(author.index)
                                if (id.startsWith("other:"))
                                    panel.dialog.toggleSelectedAuthorCustomRole(id.slice(6))
                                else
                                    panel.dialog.toggleSelectedAuthorRole(id)
                            }
                            onActionTriggered: {
                                panel.dialog.selectAuthor(author.index)
                                panel.dialog.askCustomRole()
                            }
                        }
                    }
                }

                // Nothing to list.
                Column {
                    visible: authorList.count === 0
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 14
                    topPadding: 10
                    spacing: 4
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("No authors yet")
                        color: Style.text
                        font.pixelSize: Style.fontUi
                        font.bold: true
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Use Add to credit who wrote the words or music.")
                        color: Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                }
            }
        }
    }

    GroupHeading {
        text: qsTr("Songbook")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Songbook")
                DialogField {
                    implicitWidth: Style.fieldLongW
                    escapeClearsFocus: true
                    text: panel.dialog.songbookName
                    placeholder: qsTr("Hymnal")
                    onEdited: function (v) { panel.dialog.songbookName = v }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Number")
                DialogField {
                    implicitWidth: Style.fieldShortW
                    escapeClearsFocus: true
                    text: panel.dialog.songbookEntry
                    placeholder: qsTr("123")
                    onEdited: function (v) { panel.dialog.songbookEntry = v }
                }
            }
        }
    }

    GroupHeading {
        text: qsTr("Copyright")
    }
    GroupList {
        width: panel.columnWidth

        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Copyright")
                DialogField {
                    implicitWidth: Style.fieldLongW
                    escapeClearsFocus: true
                    text: panel.dialog.copyrightText
                    placeholder: qsTr("Public domain")
                    onEdited: function (v) { panel.dialog.copyrightText = v }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("CCLI song number")
                DialogField {
                    implicitWidth: Style.fieldShortW
                    escapeClearsFocus: true
                    text: panel.dialog.ccliSongNumber
                    placeholder: qsTr("7201044")
                    onEdited: function (v) { panel.dialog.ccliSongNumber = v }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Key")
                DialogField {
                    implicitWidth: Style.fieldShortW
                    escapeClearsFocus: true
                    text: panel.dialog.keyText
                    placeholder: qsTr("G")
                    onEdited: function (v) { panel.dialog.keyText = v }
                }
            }
        }
        GroupRow {
            FormRow {
                Layout.fillWidth: true
                label: qsTr("Show on screen")
                hint: qsTr("The copyright line, where the song theme has a place for it.")
                ToggleSwitch {
                    checked: panel.dialog.showCopyright
                    onToggled: panel.dialog.showCopyright = !panel.dialog.showCopyright
                }
            }
        }
    }

    Item {
        width: panel.columnWidth
        height: 12
    }
}
