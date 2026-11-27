pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// Bibles page of Settings: one dark well holding a table of the translations,
// Name, Abbreviation and On, sorted by a click on a column's header. Over it,
// the search at the left and Delete and Import for the one picked at the
// right. A double click on a name or abbreviation edits it in place; a row's
// switch offers it for lookup or not (off, it stays imported). Its state and
// actions live on SettingsBiblesState; the file chooser and confirm are the
// dialog's.
Item {
    id: panel

    required property SettingsBiblesState dialog
    /// ``dialog.session``, typed, so calls on it are checked.
    readonly property SessionFacade session: panel.dialog.session
    readonly property bool searching: panel.dialog.bibleSearchText.trim().length > 0
    /// The translation picked, or null.
    readonly property var bible: panel.dialog.shownBible
    /// The cell open for editing: its translation (-1 for none) and column.
    property var editingId: -1
    property string editingField: ""
    /// The columns after Name, header and rows alike, padding and all.
    readonly property int abbreviationW: 116
    readonly property int onW: 56

    /// Open translation ``id``'s ``field`` ("name" or "abbreviation").
    function beginEdit(id, field) {
        panel.dialog.selectBible(id)
        panel.editingId = id
        panel.editingField = field
    }

    /// Close the cell open, keeping ``text`` when ``keep``.
    function endEdit(keep, text) {
        const id = panel.editingId
        const field = panel.editingField
        if (id === -1)
            return
        panel.editingId = -1
        panel.editingField = ""
        if (!keep)
            return
        // A moment later, as on Stacks: the list rebuilding its rows closes
        // an edit, and the change would rebuild them again mid-way.
        if (field === "name")
            Qt.callLater(panel.dialog.renameBible, id, text)
        else
            Qt.callLater(panel.dialog.setAbbreviation, id, text)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Bibles")
            text: qsTr("The Bible versions you can show on screen, imported from Zefania, "
                       + "OSIS or OpenSong files.")
        }

        // The search, and Delete and Import for the one picked, over the well.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            SearchField {
                id: bibleSearchField
                Layout.preferredWidth: Style.fieldLongW
                placeholder: qsTr("Search Bibles")
                text: panel.dialog.bibleSearchText
                onTextChanged: panel.dialog.bibleSearchText = text
                onEscaped: bibleSearchField.blur()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                size: "control"
                iconSource: Icons.url("trash", ink)
                enabled: !!panel.bible
                onClicked: panel.dialog.beginDeleteBible(panel.bible.id)
            }
            AppButton {
                size: "control"
                primary: true
                label: qsTr("Import…")
                iconSource: Icons.url("import", ink)
                enabled: !!panel.session && panel.session.ready
                onClicked: panel.dialog.openBibleImport()
            }
        }

        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: -4

            WellPane {
                id: listPane
                Layout.fillWidth: true
                Layout.fillHeight: true
                // The columns' names are the well's bar.
                showHeader: false

                // The columns' names; Name and Abbreviation sort.
                TableHeaderBar {
                    rightInset: Style.scrollGutter(bibleList)

                    TableColumnHead {
                        Layout.fillWidth: true
                        label: qsTr("Name")
                        sortable: true
                        sorted: panel.dialog.bibleSortKey === "name"
                        ascending: panel.dialog.bibleSortAsc
                        onClicked: panel.dialog.sortBiblesBy("name")
                    }
                    TableGroove {}
                    TableColumnHead {
                        Layout.preferredWidth: panel.abbreviationW
                        label: qsTr("Abbreviation")
                        sortable: true
                        sorted: panel.dialog.bibleSortKey === "abbreviation"
                        ascending: panel.dialog.bibleSortAsc
                        onClicked: panel.dialog.sortBiblesBy("abbreviation")
                    }
                    TableGroove {}
                    TableColumnHead {
                        Layout.preferredWidth: panel.onW + 6
                        label: qsTr("On")
                    }
                }

                ListView {
                    id: bibleList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The bar a gap in from the well's edge, as the Library's is.
                    Layout.rightMargin: Style.scrollGap
                    // Inset and apart, as the Libraries & Profiles wells' rows are.
                    topMargin: 6
                    bottomMargin: 6
                    leftMargin: 6
                    rightMargin: Style.scrollGutter(bibleList)
                    spacing: 3
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.dialog.bibleRows
                    ScrollBar.vertical: AppScrollBar {}

                    delegate: BibleTableRow {
                        id: bibleRow
                        required property var modelData
                        name: bibleRow.modelData.name
                        abbreviation: bibleRow.modelData.abbreviation || ""
                        on: !!bibleRow.modelData.enabled
                        selected: !!panel.bible && bibleRow.modelData.id === panel.bible.id
                        editing: panel.editingId === bibleRow.modelData.id ? panel.editingField : ""
                        abbreviationW: panel.abbreviationW
                        onW: panel.onW
                        onClicked: panel.dialog.selectBible(bibleRow.modelData.id)
                        onEditAsked: function (field) { panel.beginEdit(bibleRow.modelData.id, field) }
                        onEdited: function (field, text) { panel.endEdit(true, text) }
                        onEditCanceled: panel.endEdit(false, "")
                        onToggled: panel.dialog.setEnabled(bibleRow.modelData.id, !bibleRow.on)
                    }

                    // Nothing to list: none imported, or none match.
                    EmptyListNote {
                        visible: bibleList.count === 0
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        title: panel.searching ? qsTr("No matches") : qsTr("No Bibles imported")
                        hint: panel.searching
                          ? qsTr("Nothing found for “%1”.").arg(panel.dialog.bibleSearchText.trim())
                          : qsTr("Use Import to add a Zefania, OSIS or OpenSong XML file.")
                    }
                }
            }
        }
    }
}
