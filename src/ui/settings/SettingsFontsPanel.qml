pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import SeventhSlide

// Fonts page of Settings: one well split into the families and the faces of the
// one picked, each drawn in itself. Over it, the search at the left, and at the
// right the trash, for what is selected, and Add Fonts. Families the library names
// and has no font for lead the list, and say under their faces where they are
// used; a double click on one of those opens it.
// Its state and actions live on SettingsFontsState; the file chooser, confirm and
// what files added came to are the dialog's.
Item {
    id: panel

    required property SettingsFontsState dialog
    /// ``dialog.session``, typed, so calls on it are checked.
    readonly property SessionFacade session: panel.dialog.session
    readonly property FontController fonts: panel.dialog.fonts
    readonly property bool searching: panel.dialog.searchText.trim().length > 0
    /// The family showing, or null.
    readonly property var family: panel.dialog.shownFamily
    /// Why fonts cannot change now, or "".
    readonly property string held: !panel.fonts ? ""
                                 : !!panel.session && panel.session.readOnly
                                   ? qsTr("This library is view only on this computer, so fonts "
                                          + "cannot be added or removed here.")
                                 : panel.fonts.live
                                   ? qsTr("Something is live. Fonts can be added and removed once "
                                          + "it has stopped.")
                                 : ""

    /// The glyph for what ``use`` (``FontController::uses``) is; a service
    /// row's is its item's, as the service order shows it.
    function useIcon(use) {
        switch (String(use.kind)) {
        case "theme": return "themes"
        case "output": return "display"
        case "profile": return "person"
        case "song": return "song"
        case "freeform": return "freeform"
        case "service": {
            const type = String(use.type)
            if (type === "divider")
                return "divider"
            const found = ContentTypes.of(type)
            return found ? found.icon : "list"
        }
        }
        return "info"
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 14

        PageHeader {
            Layout.fillWidth: true
            title: qsTr("Fonts")
            text: qsTr("These are the fonts this library uses, and every computer it syncs to "
                       + "gets a copy. Only add fonts you have the right to share.")
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: -8
            visible: panel.held.length > 0
            text: panel.held
            wrapMode: Text.WordWrap
            color: Style.textWarning
            font.pixelSize: Style.fontSmall
        }

        // The search, the trash and Add Fonts, over the well.
        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            SearchField {
                id: fontSearchField
                Layout.preferredWidth: Style.fieldLongW
                placeholder: qsTr("Search fonts")
                text: panel.dialog.searchText
                onTextChanged: panel.dialog.searchText = text
                onEscaped: fontSearchField.blur()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "removeFonts"
                size: "control"
                iconSource: Icons.url("trash", ink)
                enabled: panel.dialog.canRemove
                onClicked: panel.dialog.beginRemoveSelected()
            }
            AppButton {
                objectName: "addFonts"
                size: "control"
                primary: true
                label: !!panel.fonts && panel.fonts.adding ? qsTr("Adding…") : qsTr("Add Fonts…")
                iconSource: Icons.url("import", ink)
                enabled: panel.dialog.canChange
                onClicked: panel.dialog.openAdd()
            }
        }

        ListWell {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: -4

            WellPane {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                title: qsTr("Families")

                ListView {
                    id: familyList
                    objectName: "fontFamilies"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    // The bar a gap in from the well's edge, as the Library's is.
                    Layout.rightMargin: Style.scrollGap
                    topMargin: 6
                    bottomMargin: 6
                    leftMargin: 6
                    rightMargin: Style.scrollGutter(familyList)
                    spacing: 3
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.dialog.familyRows
                    ScrollBar.vertical: AppScrollBar {}

                    // The missing, then the library's, headed only to tell them
                    // apart. The view shows every section's item whatever its
                    // ``visible``, so one with no heading takes no room.
                    section.property: "group"
                    section.delegate: Text {
                        required property string section
                        width: familyList.width - familyList.leftMargin - familyList.rightMargin
                        height: section.length > 0 ? implicitHeight : 0
                        leftPadding: 6
                        // The missing lead the list: their heading a little in
                        // from its top, level with the right's first line.
                        topPadding: section === qsTr("Missing") ? 5 : 10
                        bottomPadding: 5
                        text: section
                        color: section === qsTr("Missing") ? Style.textWarning : Style.textCaption
                        font.pixelSize: Style.fontSmall
                        font.weight: Font.DemiBold
                    }

                    delegate: PickRow {
                        required property var modelData
                        readonly property bool missing: modelData.state === "missing"
                        title: String(modelData.name)
                        subtitle: missing
                                  ? (modelData.faces > 0 ? qsTr("Its file is not here, or is damaged")
                                                         : qsTr("Showing in %1").arg(panel.fonts.standIn))
                                  : (modelData.faces === 1 ? qsTr("1 style")
                                                           : qsTr("%1 styles").arg(modelData.faces))
                        subtitleInk: missing ? Style.textWarning
                                             : (selected ? Style.textSoft : Style.textCaption)
                        readonly property bool shown: !!panel.family
                                                      && panel.family.name === modelData.name
                        // Its faces picked, the trash is theirs, not the family's.
                        selected: shown && panel.dialog.selectedFaces.length === 0
                        trail: shown && panel.dialog.selectedFaces.length > 0
                        onClicked: panel.dialog.selectFamily(String(modelData.name))
                    }

                    EmptyListNote {
                        visible: familyList.count === 0 && panel.searching
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 14
                        title: qsTr("No matches")
                        hint: qsTr("Nothing found for “%1”.").arg(panel.dialog.searchText.trim())
                    }
                }
            }

            WellPane {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ruleLeft: true
                title: panel.family ? String(panel.family.name) : ""

                ListView {
                    id: faceList
                    objectName: "fontFaces"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.rightMargin: Style.scrollGap
                    topMargin: 6
                    bottomMargin: 6
                    leftMargin: 6
                    rightMargin: Style.scrollGutter(faceList)
                    spacing: 3
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: panel.dialog.shownFaces
                    ScrollBar.vertical: AppScrollBar {}

                    // Filled and barred as a PickRow is, drawn in the face itself.
                    delegate: Rectangle {
                        id: faceRow
                        required property var modelData
                        readonly property string filename: String(faceRow.modelData.filename)
                        readonly property bool selected:
                            panel.dialog.faceSelection.contains(faceRow.filename)
                        width: ListView.view.width - faceList.leftMargin - faceList.rightMargin
                        implicitHeight: 66
                        radius: Style.radiusSmall
                        color: Style.panelRowBg(faceRow.selected, facePress.containsMouse)

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 3
                            radius: 1
                            color: Style.listAccent(faceRow.selected, facePress.containsMouse)
                        }
                        Text {
                            id: styleName
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            y: 6
                            text: String(faceRow.modelData.style)
                            color: faceRow.selected ? Style.textSoft : Style.textCaption
                            font.pixelSize: Style.fontSmall
                            elide: Text.ElideRight
                        }
                        // A file that cannot be drawn says so.
                        Text {
                            anchors.left: styleName.left
                            anchors.right: styleName.right
                            anchors.top: styleName.bottom
                            anchors.topMargin: 4
                            elide: Text.ElideRight
                            text: faceRow.modelData.loaded
                                  ? qsTr("Rejoice always.")
                                  : qsTr("Its file is not here, or is damaged.")
                            color: faceRow.modelData.loaded ? Style.text : Style.textWarning
                            font.family: faceRow.modelData.loaded ? faceRow.modelData.qtFamily
                                                                  : Application.font.family
                            font.styleName: faceRow.modelData.loaded ? faceRow.modelData.qtStyle : ""
                            font.weight: faceRow.modelData.loaded ? faceRow.modelData.weight : Font.Normal
                            font.italic: faceRow.modelData.loaded && faceRow.modelData.italic
                            font.pixelSize: faceRow.modelData.loaded ? 20 : Style.fontBody
                        }
                        MouseArea {
                            id: facePress
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onPressed: function (mouse) {
                                panel.dialog.faceSelection.press(faceRow.filename, mouse.modifiers)
                            }
                            onReleased: panel.dialog.faceSelection.release()
                            onCanceled: panel.dialog.faceSelection.keepOnRelease()
                        }
                    }

                    // Under the faces, and in their place for a family with none:
                    // what shows for a missing family, how to fix it, and what
                    // uses it.
                    footer: Column {
                        width: faceList.width - faceList.leftMargin - faceList.rightMargin

                        // At the top, as far in as the Missing heading beside it,
                        // and in line with what uses it below.
                        Column {
                            width: parent.width
                            visible: !!panel.family && faceList.count === 0
                            leftPadding: 6
                            rightPadding: 6
                            topPadding: 4
                            spacing: 4

                            Text {
                                width: parent.width - parent.leftPadding - parent.rightPadding
                                wrapMode: Text.WordWrap
                                text: qsTr("No font for %1").arg(panel.family ? String(panel.family.name) : "")
                                color: Style.text
                                font.pixelSize: Style.fontUi
                                font.bold: true
                            }
                            Text {
                                width: parent.width - parent.leftPadding - parent.rightPadding
                                wrapMode: Text.WordWrap
                                text: qsTr("Text in it shows in %1 until its font files are added with "
                                           + "Add Fonts.").arg(panel.fonts ? panel.fonts.standIn : "")
                                color: Style.textCaption
                                font.pixelSize: Style.fontBody
                            }
                        }

                        // What uses it, a row each, under a heading set as the
                        // families' are. A double click opens it.
                        Column {
                            objectName: "fontUses"
                            width: parent.width
                            visible: panel.dialog.shownMissing
                            topPadding: faceList.count === 0 ? 14 : 10
                            spacing: 3

                            Text {
                                width: parent.width
                                leftPadding: 6
                                rightPadding: 6
                                bottomPadding: 2
                                wrapMode: Text.WordWrap
                                text: panel.dialog.shownUses.length === 0
                                      ? qsTr("Nothing saved in this library uses it: something shown "
                                             + "asked for it.")
                                      : panel.dialog.shownUses.length === 1
                                        ? qsTr("Used in 1 place")
                                        : qsTr("Used in %1 places").arg(panel.dialog.shownUses.length)
                                color: Style.textCaption
                                font.pixelSize: Style.fontSmall
                                font.weight: panel.dialog.shownUses.length === 0 ? Font.Normal
                                                                                 : Font.DemiBold
                            }

                            Repeater {
                                model: panel.dialog.shownUses
                                // Its glyph says what it is; what holds it (a
                                // theme's output, a row's service) is under it.
                                delegate: PickRow {
                                    id: useRow
                                    required property var modelData
                                    width: parent.width
                                    iconSource: Icons.url(panel.useIcon(useRow.modelData), Style.textSoft)
                                    title: String(useRow.modelData.name).length > 0
                                           ? String(useRow.modelData.name) : qsTr("Untitled")
                                    subtitle: String(useRow.modelData.within)
                                    onDoubleClicked: panel.dialog.openUse(useRow.modelData)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
