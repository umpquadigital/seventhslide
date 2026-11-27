pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeventhSlide

// The Fields window: the document's fields in a table (name, value, where
// used), Delete and New under it. A text box being typed into gets a + per
// row, which puts the field's token at the caret (its split arrow offers the
// form that hides the line when the field is empty), and From selection,
// which makes a field of the text selected and puts its token in its place.
// A theme has no fields of its own: the same window lists the tokens a
// service fills in, name only, with the same + column.
Item {
    id: root
    required property SlideEditState dialog
    /// The stage, for the caret.
    required property SlideStage stage
    readonly property SlideEditSession edit: root.dialog.edit
    readonly property bool open: card.open
    /// A theme: the service's tokens, read-only.
    readonly property bool tokensOnly: root.dialog.isTheme
    property string selectedName: ""
    property string errorText: ""

    function show() {
        root.errorText = ""
        card.open = true
    }
    function hide() { card.open = false }
    function report(err) {
        root.errorText = String(err || "")
        if (root.errorText.length)
            errorTimer.restart()
        return root.errorText.length === 0
    }
    /// A name no field has yet.
    function freshName() {
        const names = root.edit ? (root.edit.fields.names() || []) : []
        let n = 1
        while (names.indexOf("field-" + n) >= 0)
            ++n
        return "field-" + n
    }
    /// ``{name}`` at the caret of the box being typed into; ``hide`` makes it
    /// ``{-name}``, the form that hides its line when the field is empty.
    function insertToken(name, hide) {
        if (!root.typing)
            return
        root.stage.insertAtCaret((hide ? "{-" : "{") + name + "}")
    }
    readonly property bool typing: root.dialog.textEditing
    readonly property bool hasTextSelection: root.typing && root.stage
                                             && String(root.stage.selectedText() || "").length > 0
                                             && root.dialog.editSelTick >= 0
    /// A theme's tokens as the table's rows.
    readonly property var tokenRows: {
        const names = root.tokensOnly && root.edit ? (root.edit.fields.tokenNames() || []) : []
        const rows = []
        for (let i = 0; i < names.length; ++i)
            rows.push({ name: names[i], value: "", unused: false, token: "{" + names[i] + "}" })
        return rows
    }

    Timer {
        id: errorTimer
        interval: 3200
        onTriggered: root.errorText = ""
    }

    // The insert arrow's menu, for the row that opened it.
    AppMenu {
        id: insertMenu
        property string forName: ""
        alignRight: true
        MenuItemRow {
            label: qsTr("Insert")
            onTriggered: { insertMenu.close(); root.insertToken(insertMenu.forName, false) }
        }
        MenuItemRow {
            objectName: "fieldInsertHidden"
            label: qsTr("Insert, hiding the line when empty")
            onTriggered: { insertMenu.close(); root.insertToken(insertMenu.forName, true) }
        }
    }

    AppDialog {
        id: card
        title: qsTr("Fields")
        acknowledge: true
        cardWidth: 640
        maxCardHeight: 520

        RowLayout {
            Layout.fillWidth: true
            visible: !root.tokensOnly
            spacing: 8
            AppButton {
                id: fromSelection
                objectName: "fieldsFromSelection"
                size: "compact"
                iconSource: Icons.url("braces", fromSelection.ink, 2)
                label: qsTr("From selection")
                enabled: root.hasTextSelection
                onClicked: {
                    const text = String(root.stage.selectedText() || "")
                    if (!text.length)
                        return
                    // The token goes in first, while the selection is live:
                    // the field's edits reload the box's words from the
                    // draft, which then carries it. (A fresh name is always
                    // taken.)
                    const name = root.freshName()
                    root.stage.replaceSelection("{" + name + "}")
                    if (!root.report(root.edit.fields.add(name)))
                        return
                    root.report(root.edit.fields.set(name, text))
                    root.selectedName = name
                }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                id: deleteField
                objectName: "fieldsDelete"
                size: "compact"
                iconSource: Icons.url("trash", deleteField.ink, 2)
                enabled: root.selectedName.length > 0
                onClicked: {
                    const name = root.selectedName
                    if (root.report(root.edit.fields.remove(name)))
                        root.selectedName = ""
                }
            }
            AppButton {
                id: newField
                objectName: "fieldsNew"
                size: "compact"
                primary: true
                iconSource: Icons.url("plus", newField.ink, 2)
                label: qsTr("New")
                onClicked: {
                    const name = root.freshName()
                    if (root.report(root.edit.fields.add(name)))
                        root.selectedName = name
                }
            }
        }
        Rectangle {
            id: well
            Layout.fillWidth: true
            Layout.fillHeight: true
            implicitHeight: 300
            radius: Style.radiusLarge
            color: Style.wellBg
            border.color: Style.wellEdge
            clip: true

            readonly property int nameW: 150
            readonly property int usedW: root.tokensOnly ? 0 : 80
            readonly property int plusW: 60
            readonly property int valueW: root.tokensOnly ? 0 : width - 2 - 10 - nameW - usedW - plusW

            Rectangle {
                id: head
                x: 1
                y: 1
                width: parent.width - 2
                height: 30
                color: Style.wellHead
                Row {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    Text { width: root.tokensOnly ? head.width - 10 - well.plusW : well.nameW; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Name"); color: Style.textSoft; font.pixelSize: Style.fontBody; font.bold: true }
                    Text { visible: !root.tokensOnly; width: well.valueW; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Value"); color: Style.textSoft; font.pixelSize: Style.fontBody; font.bold: true }
                    Text { visible: !root.tokensOnly; width: well.usedW; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Used"); color: Style.textSoft; font.pixelSize: Style.fontBody; font.bold: true }
                    Text { width: well.plusW; anchors.verticalCenter: parent.verticalCenter; text: qsTr("Insert"); color: Style.textSoft; font.pixelSize: Style.fontBody; font.bold: true }
                }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Style.groupRule }
            }

            ListView {
                id: list
                objectName: "fieldsList"
                anchors.fill: parent
                anchors.topMargin: head.height + 5
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                anchors.bottomMargin: 5
                spacing: 3
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {}
                model: root.tokensOnly ? root.tokenRows
                                       : (root.edit && root.edit.active ? root.edit.fields.list : null)
                delegate: Rectangle {
                    id: fieldRow
                    required property int index
                    required property string name
                    required property string value
                    required property bool unused
                    required property string token
                    readonly property bool selected: !root.tokensOnly && root.selectedName === fieldRow.name
                    // Follows the document, which usage() alone does not.
                    readonly property int uses: root.edit && root.edit.revision >= 0 && !root.tokensOnly
                                                ? root.edit.fields.usage(fieldRow.name) : 0
                    width: list.width - Style.scrollGutter(list)
                    height: 38
                    radius: Style.radiusSmall
                    color: fieldRow.selected ? Style.panelRowSel : Style.groupBg
                    Rectangle {
                        visible: fieldRow.selected
                        width: 3
                        height: parent.height
                        color: Style.selectedBar
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: function (mouse) {
                            if (!root.tokensOnly)
                                root.selectedName = fieldRow.name
                            mouse.accepted = false
                        }
                    }
                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        spacing: 0
                        // A field's name is typed; a token's is read.
                        DialogField {
                            objectName: "fieldName"
                            visible: !root.tokensOnly
                            width: well.nameW - 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: fieldRow.name
                            onCommitted: function (v) {
                                const to = root.edit ? root.edit.fields.sanitize(v) : ""
                                if (!to.length || to === fieldRow.name)
                                    return
                                if (root.report(root.edit.fields.rename(fieldRow.name, to)))
                                    root.selectedName = to
                            }
                        }
                        Text {
                            objectName: "tokenName"
                            visible: root.tokensOnly
                            width: fieldRow.width - 6 - well.plusW
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 4
                            text: fieldRow.name
                            color: Style.text
                            font.pixelSize: Style.fontBody
                            elide: Text.ElideRight
                        }
                        Item { visible: !root.tokensOnly; width: 8; height: 1 }
                        DialogField {
                            objectName: "fieldValue"
                            visible: !root.tokensOnly
                            width: well.valueW - 8
                            anchors.verticalCenter: parent.verticalCenter
                            text: fieldRow.value
                            onCommitted: function (v) { root.report(root.edit.fields.set(fieldRow.name, v)) }
                        }
                        Item {
                            visible: !root.tokensOnly
                            width: well.usedW
                            height: parent.height
                            Text {
                                visible: fieldRow.uses > 0
                                anchors.verticalCenter: parent.verticalCenter
                                x: 2
                                text: String(fieldRow.uses)
                                color: Style.textFaint
                                font.pixelSize: Style.fontSmall
                            }
                            StatusBadge {
                                visible: fieldRow.uses <= 0
                                anchors.verticalCenter: parent.verticalCenter
                                x: 2
                                capitals: false
                                text: qsTr("Not used")
                            }
                        }
                        AppButton {
                            id: insertButton
                            objectName: "fieldInsert"
                            anchors.verticalCenter: parent.verticalCenter
                            size: "compact"
                            splitMenu: true
                            menuOpen: insertMenu.visible && insertMenu.forName === fieldRow.name
                            iconSource: Icons.url("plus", insertButton.ink, 2)
                            enabled: root.typing
                            onClicked: root.insertToken(fieldRow.name, false)
                            onMenuClicked: {
                                insertMenu.forName = fieldRow.name
                                insertMenu.anchorItem = insertButton
                                insertMenu.opener = insertButton
                                insertMenu.openAt()
                            }
                        }
                    }
                }
                EmptyListNote {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 20
                    visible: list.count === 0
                    title: qsTr("No fields yet")
                    hint: qsTr("New makes one; its token goes wherever its value should show.")
                }
            }
        }

        Text {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            wrapMode: Text.WordWrap
            text: root.errorText
            color: Style.btnDangerInk
            font.pixelSize: Style.fontSmall
        }

    }
}
