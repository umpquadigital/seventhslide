pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import SeventhSlide

// The slide editor: one window for a Freeform item, a text theme and a
// background theme. Edits go through session.slideEdit; SlideEditState is the
// window's view-model. This is the frame around its parts (title bar, tab
// row, ribbon, sorter, stage, sidebar, footer) and what only a view has: the
// pickers, confirms and file choosers the state asks for, focus and keys.
Item {
    id: root
    required property SessionFacade session
    readonly property SlideEditSession edit: root.session ? root.session.slideEdit : null

    visible: !!root.edit && root.edit.active
    z: Style.editorZ
    parent: Window.window ? Window.window.contentItem : parent
    width: visible && parent ? parent.width : 0
    height: visible && parent ? parent.height : 0

    SlideEditState {
        id: dialog
        session: root.session
        onFlattenAsked: flattenConfirm.open = true
        onFieldsAsked: fieldsWindow.show()
        onSlideBgImageAsked: slideBgImageDialog.open()
        onSlideBgVideoAsked: slideBgVideoDialog.open()
        onMediaFileAsked: function (forWhat) {
            mediaFileDialog.forWhat = forWhat
            mediaFileDialog.open()
        }
        onSlideThemeAsked: {
            slideThemePicker.titleText = qsTr("Select Background Theme")
            slideThemePicker.subtitleText = qsTr("Choose a background theme for this slide.")
            slideThemePicker.openFor(root.edit.output, "bg", "")
        }
        onAssetAsked: function (slot, video) {
            assetDialog.slot = slot
            assetDialog.video = video
            assetDialog.open()
        }
        onKeyFocusRequested: root.forceActiveFocus()
    }

    /// A file chooser's filters for pictures or videos: the files the
    /// library takes (session.media.mediaFilePatterns), then any file.
    function mediaFilters(video) {
        const patterns = root.session.media.mediaFilePatterns(video ? MediaKind.Video : MediaKind.Image)
        return [(video ? qsTr("Videos (%1)") : qsTr("Images (%1)")).arg(patterns), qsTr("All files (*)")]
    }
    /// Close every picker, settle the stage, end edits in progress. A menu
    /// closes itself when the window hides.
    function closeOverlays() {
        stage.finishGesture()
        flattenConfirm.close()
        discardConfirm.close()
        if (slideThemePicker.open)
            slideThemePicker.close()
        dialog.closeEdits()
    }
    function cancelEditor() {
        root.closeOverlays()
        fieldsWindow.hide()
        dialog.cancel()
    }
    /// Cancel as the operator asks for it: unsaved work is not lost unasked.
    function requestCancel() {
        if (dialog.editorDirty)
            discardConfirm.open = true
        else
            root.cancelEditor()
    }
    function saveEditor() {
        if (dialog.save())
            fieldsWindow.hide()
    }
    /// The editing begins as the window shows and ends as it hides. Made the
    /// first time a document opens (Main's LazyLoader), it may be showing
    /// already when it is made.
    property bool begun: false
    function followShown() {
        if (root.visible === root.begun)
            return
        root.begun = root.visible
        if (root.begun) {
            dialog.begin()
            root.forceActiveFocus()
        } else {
            fieldsWindow.hide()
            root.closeOverlays()
            dialog.end()
        }
    }
    onVisibleChanged: root.followShown()
    Component.onCompleted: root.followShown()

    readonly property var tabs: dialog.isFreeform
        ? [{ id: "home", label: qsTr("Home") }, { id: "insert", label: qsTr("Insert") },
           { id: "design", label: qsTr("Design") }, { id: "transitions", label: qsTr("Transitions") },
           { id: "animations", label: qsTr("Animations") }, { id: "cues", label: qsTr("Cues") }]
        : dialog.isBgTheme
        // A background theme has nothing to type or put on it: its fill is all.
        ? [{ id: "design", label: qsTr("Design") }, { id: "transitions", label: qsTr("Transitions") }]
        : [{ id: "home", label: qsTr("Home") }, { id: "insert", label: qsTr("Insert") },
           { id: "design", label: qsTr("Design") }, { id: "transitions", label: qsTr("Transitions") }]

    /// The outputs the picker offers: a Freeform's decks, or every output.
    function outputOptions() {
        if (!root.edit)
            return []
        const out = []
        if (dialog.isFreeform) {
            const names = root.edit.slides.deckOutputs() || []
            for (let i = 0; i < names.length; ++i)
                out.push({ value: names[i], label: names[i] })
            return out
        }
        const model = root.session.outputs
        for (let r = 0; r < model.rowCount(); ++r) {
            const name = model.data(model.index(r, 0), OutputListModel.NameRole)
            out.push({ value: name, label: name })
        }
        return out
    }
    /// What the open theme is, by its kind.
    readonly property string themeTitle: {
        switch (root.edit && root.edit.theme ? root.edit.theme.kind : "") {
        case "song":
            return qsTr("Song Theme")
        case "bible":
            return qsTr("Bible Theme")
        case "media":
            return qsTr("Media Theme")
        default:
            return qsTr("Background Theme")
        }
    }

    /// A text theme's context, drawn and named as ContentTypes has its kind.
    /// Titles, a song's title slides, is the one context that is not a kind.
    function contextGlyph(id) {
        const type = ContentTypes.of(id)
        return type ? type.icon : id === "titles" ? "titles" : "display"
    }
    function contextLabel(id) {
        const type = ContentTypes.of(id)
        return type ? type.singular : id === "titles" ? qsTr("Title slides") : String(id)
    }

    ModalScrim {
        anchors.fill: parent
        tint: Style.sheetScrim
        onClicked: root.closeOverlays()
    }

    Rectangle {
        id: box
        objectName: "slideEditorBox"
        anchors.centerIn: parent
        width: Math.max(0, Math.min(parent.width - 40, Style.editorShellWidth))
        // One size however the panes beside the stage are set: tall enough for
        // the slide with the sidebar shut, the stage shrinking when it opens.
        // The title, tab row, footer and the body's margins are fixed.
        height: Math.max(0, Math.min(parent.height - 24, Style.editorShellHeight,
                                     Math.max(Style.editorShellMinHeight,
                                              44 + 34 + ribbon.implicitHeight + 44 + 16
                                              + stage.fittedHeight(box.width - 16 - 196 - 8))))
        readonly property int sorterWidth: dialog.edit && dialog.edit.cues.lineControl ? 280 : 196
        readonly property int sidebarWidth: 288
        readonly property bool sidebarShown: dialog.sidebarOpen && dialog.mode !== "bg"
        radius: Style.radiusLarge
        color: Style.panelBg
        border.color: Style.modalEdge
        clip: true

        MouseArea {
            anchors.fill: parent
            onClicked: root.forceActiveFocus()
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 1
            spacing: 0

            // ---- title bar: what is edited, undo/redo, its name, Fields
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                color: Style.headerBg
                topLeftRadius: Style.radiusLarge - 1
                topRightRadius: Style.radiusLarge - 1
                z: 10
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Style.border }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 14
                    spacing: 6
                    Text {
                        objectName: "slideEditorTitle"
                        Layout.rightMargin: 10
                        text: dialog.isFreeform ? qsTr("Freeform") : root.themeTitle
                        color: Style.textBright
                        font.pixelSize: Style.fontHeading
                        font.bold: true
                    }
                    AppButton {
                        id: undoButton
                        objectName: "slideEditorUndo"
                        size: "compact"
                        iconSource: Icons.url("undo", undoButton.ink, 2)
                        enabled: !!root.edit && root.edit.canUndo
                        onClicked: { dialog.closeEdits(); dialog.report(root.edit.undo()) }
                    }
                    AppButton {
                        id: redoButton
                        objectName: "slideEditorRedo"
                        size: "compact"
                        iconSource: Icons.url("redo", redoButton.ink, 2)
                        enabled: !!root.edit && root.edit.canRedo
                        onClicked: { dialog.closeEdits(); dialog.report(root.edit.redo()) }
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        Layout.leftMargin: 10
                        text: qsTr("Name")
                        color: Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                    DialogField {
                        objectName: "slideEditorName"
                        Layout.preferredWidth: 220
                        text: dialog.nameDraft
                        onEdited: function (v) {
                            dialog.nameSuppress = true
                            dialog.nameDraft = v
                        }
                        onCommitted: function (v) {
                            dialog.nameSuppress = false
                            dialog.report(root.edit.setName(v))
                        }
                    }
                    Text {
                        visible: dialog.isTheme
                        Layout.leftMargin: 10
                        text: qsTr("Tags")
                        color: Style.textCaption
                        font.pixelSize: Style.fontBody
                    }
                    TagField {
                        objectName: "slideEditorTags"
                        visible: dialog.isTheme
                        Layout.preferredWidth: 220
                        Layout.maximumHeight: 36
                        clip: true
                        placeholder: qsTr("Add a tag")
                        tags: root.edit && dialog.isTheme ? root.edit.theme.tags : []
                        onAdded: function (text) { root.edit.theme.setTags(root.edit.theme.tags.concat([text])) }
                        onRemoved: function (index) {
                            const next = root.edit.theme.tags.slice()
                            next.splice(index, 1)
                            root.edit.theme.setTags(next)
                        }
                    }
                    RibbonButton {
                        objectName: "slideEditorFields"
                        size: "control"
                        visible: dialog.isFreeform
                        icon: "braces"
                        // The count follows the document: rowCount() alone
                        // says nothing when it changes.
                        label: qsTr("Fields · %1").arg(root.edit && root.edit.active && root.edit.revision >= 0
                                                       ? root.edit.fields.list.rowCount() : 0)
                        on: fieldsWindow.open
                        onClicked: fieldsWindow.open ? fieldsWindow.hide() : fieldsWindow.show()
                    }
                }
            }

            // ---- tab row: output picker, the ribbon's tabs, sidebar toggle
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                color: Style.libraryTabsBg
                z: 9
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: Style.border }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 0
                    RibbonDropdown {
                        objectName: "slideEditorOutput"
                        bar: true
                        // A theme belongs to its output: only a deck can change.
                        visible: dialog.isFreeform
                        Layout.alignment: Qt.AlignVCenter
                        icon: "display"
                        fieldWidth: 184
                        // A deck added or taken away is a change to the document.
                        options: root.edit && root.edit.revision >= 0 ? root.outputOptions() : []
                        value: root.edit ? root.edit.output : ""
                        onPicked: function (v) {
                            dialog.closeEdits()
                            root.edit.setOutput(v)
                        }
                    }
                    RibbonDropdown {
                        objectName: "slideEditorContext"
                        bar: true
                        Layout.alignment: Qt.AlignVCenter
                        visible: dialog.isTextTheme && !!root.edit && root.edit.contexts.length > 1
                        icon: root.contextGlyph(root.edit ? root.edit.context : "")
                        fieldWidth: 150
                        options: {
                            const list = root.edit ? root.edit.contexts : []
                            const out = []
                            for (let i = 0; i < list.length; ++i)
                                out.push({ value: list[i], label: root.contextLabel(list[i]) })
                            return out
                        }
                        value: root.edit ? root.edit.context : ""
                        onPicked: function (v) {
                            dialog.closeEdits()
                            root.edit.setContext(v)
                        }
                    }
                    Item { Layout.preferredWidth: 8 }
                    Repeater {
                        model: root.tabs
                        delegate: Item {
                            id: tabCell
                            required property var modelData
                            readonly property bool active: dialog.ribbonTab === modelData.id
                            objectName: "slideEditorTab_" + modelData.id
                            Layout.fillHeight: true
                            Layout.preferredWidth: tabText.implicitWidth + 24
                            Rectangle {
                                anchors.fill: parent
                                color: tabHover.containsMouse && !tabCell.active ? Style.tabHoverBg : "transparent"
                            }
                            Text {
                                id: tabText
                                anchors.centerIn: parent
                                text: tabCell.modelData.label
                                color: tabCell.active ? Style.text
                                     : (tabHover.containsMouse ? Style.textHover : Style.textMuted)
                                font.pixelSize: Style.fontBody
                                font.bold: tabCell.active
                            }
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 2
                                color: tabCell.active ? Style.accent : "transparent"
                            }
                            MouseArea {
                                id: tabHover
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: dialog.ribbonTab = tabCell.modelData.id
                            }
                        }
                    }
                    Item { Layout.fillWidth: true }
                    // A background theme has nothing to select: no sidebar.
                    AppButton {
                        id: sidebarToggle
                        objectName: "slideEditorSidebarToggle"
                        visible: dialog.mode !== "bg"
                        Layout.alignment: Qt.AlignVCenter
                        size: "compact"
                        iconSource: Icons.url("sidebar", sidebarToggle.ink, 2)
                        toggled: dialog.sidebarOpen
                        toggledKind: "on"
                        onClicked: dialog.sidebarOpen = !dialog.sidebarOpen
                    }
                }
            }

            RibbonBar {
                id: ribbon
                Layout.fillWidth: true
                z: 8
                dialog: dialog
            }

            // ---- body: sorter, stage, sidebar
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.margins: 8
                spacing: 8
                SlideSorter {
                    objectName: "slideSorter"
                    visible: dialog.isFreeform
                    // Under line control its rows carry each cue's words.
                    Layout.preferredWidth: box.sorterWidth
                    Layout.fillHeight: true
                    dialog: dialog
                }
                SlideStage {
                    id: stage
                    objectName: "slideStage"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    dialog: dialog
                }
                SlideSidebar {
                    objectName: "slideSidebar"
                    visible: box.sidebarShown
                    Layout.preferredWidth: box.sidebarWidth
                    Layout.fillHeight: true
                    dialog: dialog
                }
            }

            // ---- footer: zoom at the left, Cancel and Save at the right
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 44
                color: Style.headerBg
                bottomLeftRadius: Style.radiusLarge - 1
                bottomRightRadius: Style.radiusLarge - 1
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Style.border }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 14
                    spacing: 8
                    AppButton {
                        id: fitButton
                        objectName: "slideEditorZoomFit"
                        size: "compact"
                        iconSource: Icons.url("fit", fitButton.ink, 2)
                        onClicked: dialog.zoom = 0
                    }
                    Text {
                        Layout.preferredWidth: 12
                        horizontalAlignment: Text.AlignHCenter
                        text: "\u2212"
                        color: Style.textCaption
                        font.pixelSize: 14
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.zoom = dialog.clampZoom(stage.stageScale - 0.1)
                        }
                    }
                    Item {
                        id: zoomTrack
                        objectName: "slideEditorZoom"
                        Layout.preferredWidth: 120
                        Layout.preferredHeight: 20
                        readonly property real minZ: dialog.minZoom
                        readonly property real maxZ: dialog.maxZoom
                        readonly property real shown: stage.stageScale
                        readonly property real k: Math.max(0, Math.min(1, (shown - minZ) / (maxZ - minZ)))
                        function pick(mx) {
                            const t = Math.max(0, Math.min(1, mx / width))
                            dialog.zoom = Math.round((minZ + t * (maxZ - minZ)) * 100) / 100
                        }
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: 4
                            radius: 2
                            color: Style.sliderTrack
                            border.color: Style.sliderTrackEdge
                        }
                        Rectangle {
                            x: zoomTrack.k * parent.width - width / 2
                            anchors.verticalCenter: parent.verticalCenter
                            width: 14
                            height: 14
                            radius: 7
                            color: Style.sliderKnob
                            border.color: Style.sliderKnobEdge
                        }
                        MouseArea {
                            anchors.fill: parent
                            onPressed: function (mouse) { zoomTrack.pick(mouse.x) }
                            onPositionChanged: function (mouse) { if (pressed) zoomTrack.pick(mouse.x) }
                        }
                    }
                    Text {
                        Layout.preferredWidth: 12
                        horizontalAlignment: Text.AlignHCenter
                        text: "+"
                        color: Style.textCaption
                        font.pixelSize: 14
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -4
                            cursorShape: Qt.PointingHandCursor
                            onClicked: dialog.zoom = dialog.clampZoom(stage.stageScale + 0.1)
                        }
                    }
                    Text {
                        Layout.preferredWidth: 38
                        text: Math.round(zoomTrack.shown * 100) + " %"
                        color: Style.textCaption
                        font.pixelSize: Style.fontSmall
                    }
                    Text {
                        objectName: "slideEditorStatus"
                        Layout.fillWidth: true
                        Layout.leftMargin: 12
                        text: dialog.errorText.length ? dialog.errorText : dialog.statusMessage
                        color: dialog.errorText.length ? Style.btnDangerInk : Style.textMuted
                        font.pixelSize: Style.fontSmall
                        elide: Text.ElideRight
                    }
                    AppButton {
                        objectName: "slideEditorCancel"
                        label: qsTr("Cancel")
                        minWidth: 76
                        onClicked: root.requestCancel()
                    }
                    AppButton {
                        objectName: "slideEditorSave"
                        primary: true
                        label: qsTr("Save")
                        minWidth: 76
                        enabled: dialog.editorDirty
                        onClicked: root.saveEditor()
                    }
                }
            }
        }
    }

    // ---- what the state asks for
    ThemePickerDialog {
        id: slideThemePicker
        onOpenChanged: if (!open) root.forceActiveFocus()
        session: root.session
        showInherit: false
        onAccepted: function (themeId) {
            if (!root.edit)
                return
            // Copied into the slide, not linked.
            dialog.report(String(themeId || "").length
                          ? root.edit.slides.copyBackgroundTheme(themeId)
                          : root.edit.slides.setBackground("none", ""))
        }
    }
    FileDialog {
        id: slideBgImageDialog
        objectName: "slideBgImageDialog"
        title: qsTr("Slide background image")
        fileMode: FileDialog.OpenFile
        nameFilters: root.mediaFilters(false)
        onAccepted: dialog.importSlideBackground(selectedFile.toString(), false)
    }
    FileDialog {
        id: slideBgVideoDialog
        objectName: "slideBgVideoDialog"
        title: qsTr("Slide background video")
        fileMode: FileDialog.OpenFile
        nameFilters: root.mediaFilters(true)
        onAccepted: dialog.importSlideBackground(selectedFile.toString(), true)
    }
    FileDialog {
        id: mediaFileDialog
        objectName: "mediaFileDialog"
        /// As SlideEditState.mediaFileAsked's.
        property string forWhat: "object"
        title: {
            switch (mediaFileDialog.forWhat) {
            case "video":
                return qsTr("Insert video")
            case "object":
                return qsTr("Insert picture")
            case "video_file":
                return qsTr("Choose video")
            case "poster":
                return qsTr("Choose poster")
            default:
                return qsTr("Choose picture")
            }
        }
        fileMode: FileDialog.OpenFile
        nameFilters: root.mediaFilters(dialog.mediaUseIsVideo(mediaFileDialog.forWhat))
        onAccepted: {
            dialog.importMediaFile(selectedFile.toString(), mediaFileDialog.forWhat)
            root.forceActiveFocus()
        }
        // The keys come back to the editor with the chooser gone.
        onRejected: root.forceActiveFocus()
    }
    FileDialog {
        id: assetDialog
        objectName: "slideAssetDialog"
        property string slot: "bg"
        /// A video for the slot, else a picture.
        property bool video: false
        fileMode: FileDialog.OpenFile
        nameFilters: root.mediaFilters(assetDialog.video)
        onAccepted: root.edit.theme.uploadAsset(assetDialog.slot, selectedFile.toString())
    }
    AppDialog {
        id: flattenConfirm
        onOpenChanged: if (!open) root.forceActiveFocus()
        title: qsTr("Turn off Line control?")
        blurb: qsTr("Outputs have different slide counts. Flattening keeps this output’s count and pads or truncates the others.")
        acceptLabel: qsTr("Flatten")
        danger: false
        onAccepted: dialog.report(root.edit.cues.setLineControl(false, true))
    }
    DiscardConfirm {
        id: discardConfirm
        onOpenChanged: if (!open) root.forceActiveFocus()
        onAccepted: root.cancelEditor()
    }
    FieldsWindow {
        id: fieldsWindow
        dialog: dialog
        stage: stage
    }

    Keys.onEscapePressed: function (event) {
        event.accepted = true
        if (dialog.cueInlineRenameIndex >= 0) {
            dialog.cancelCueInlineRename()
            return
        }
        if (dialog.textEditing) {
            dialog.endTextEdit(true)
            return
        }
        if (flattenConfirm.open || discardConfirm.open || slideThemePicker.open || fieldsWindow.open) {
            root.closeOverlays()
            fieldsWindow.hide()
            return
        }
        root.requestCancel()
    }
    CanvasEditorKeys {
        id: canvasKeys
        editor: root.edit ? root.edit.objects : null
        active: !dialog.textEditing && dialog.cueInlineRenameIndex < 0 && !fieldsWindow.open
        copyWithoutSelection: function () { return dialog.isFreeform ? root.edit.slides.copy() : "" }
        copiedWithoutSelection: function () {
            const n = root.edit.slides.selectedCount()
            return n > 1 ? qsTr("%1 slides copied to clipboard").arg(n) : qsTr("Slide copied to clipboard")
        }
        onRefused: function (message) { dialog.showStatus(message) }
        onDone: function (message) { dialog.showStatus(message) }
    }
    Keys.onPressed: function (event) {
        if (canvasKeys.handle(event))
            event.accepted = true
    }
    focus: visible && !dialog.textEditing
}
