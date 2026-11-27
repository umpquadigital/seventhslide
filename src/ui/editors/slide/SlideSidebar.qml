pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import SeventhSlide

// The sidebar: Properties (the selection's, each part a section that folds;
// which sections show is what the selection is) and, for a Freeform item,
// Build (the slide's animations) and Notes (its speaker notes). A theme has
// no tabs, having neither.
Item {
    id: side
    required property SlideEditState dialog
    readonly property bool tabbed: side.dialog.isFreeform
    readonly property string tab: side.tabbed ? side.dialog.sidebarTab : "properties"
    readonly property SessionFacade session: side.dialog.session
    readonly property var sel: side.dialog.sel
    readonly property bool has: side.dialog.hasSel
    readonly property string type: side.dialog.selType
    readonly property string binding: side.dialog.selBinding
    readonly property bool textLike: side.has && side.dialog.selSupports("font")
    /// A shape, or a box whose words can have a frame behind them.
    readonly property bool fillable: side.has && side.dialog.selSupports("fill")
    /// Anything a shadow, glow or soft edge can go round.
    readonly property bool effectable: side.has && side.dialog.selSupports("effects")
    readonly property bool lyrics: side.dialog.isTextTheme && side.binding === "lyrics"
    readonly property bool passage: side.dialog.isTextTheme && side.binding === "bible_text"
    /// A theme's lines follow: the active one in its own type.
    readonly property bool following: Number(side.dialog.setting("follow_lines")) > 0
    /// A box whose words are styled run by run.
    readonly property bool runs: side.dialog.selSupports("runs")
    /// A text box of a Freeform slide's or a theme's own words.
    readonly property bool staticText: side.type === "text" && side.binding === "static"
    /// A Freeform text box that shows its cues' lines.
    readonly property bool linkedText: side.type === "text" && !!side.sel.cue_linked
    readonly property string fillKind: String(side.sel.fill_kind)
    readonly property bool arrow: side.sel.shape_kind === "arrow" || side.sel.shape_kind === "double_arrow"
    /// A drawn shape not joined up: a line, with ends and no fill.
    readonly property bool shapeOpen: side.has && side.sel.shape_kind === "drawn" && !side.sel.shape_closed
    readonly property var lineEnds: [
        { value: "none", label: qsTr("None") },
        { value: "arrow", label: qsTr("Arrow") },
        { value: "dot", label: qsTr("Dot") }
    ]
    /// How a picture or a video fills its box.
    readonly property var fits: [
        { value: "contain", label: qsTr("Fit inside") },
        { value: "cover", label: qsTr("Fill, cropping") },
        { value: "fill", label: qsTr("Stretch") }
    ]

    function setProp(name, value) { side.dialog.setProp(name, value) }

    readonly property int tabIndex: side.tab === "notes" ? 2 : side.tab === "build" ? 1 : 0
    readonly property var tabIds: ["properties", "build", "notes"]

    // The tabs: a switch over the sidebar's wells, its options sharing the
    // width. Not the panels' underline strip, which heads a bar over one well.
    SegmentedControl {
        id: tabs
        objectName: "sideTabs"
        visible: side.tabbed
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        // Its right edge on the sections', short of Properties' scroll bar.
        anchors.rightMargin: side.tab === "properties" ? Style.scrollGutter(flick) : 0
        height: visible ? implicitHeight : 0
        stretch: true
        options: [qsTr("Properties"), qsTr("Build"), qsTr("Notes")]
        current: side.tabIndex
        onChosen: function (index) { side.dialog.sidebarTab = side.tabIds[index] }
    }

    // A tab's well: the slide's number at its head, ``tools`` at the head's
    // right, and the tab's own content under it.
    component SlidePane: Rectangle {
        id: pane
        /// The slide the head names, from 0; none when -1.
        property int slideIndex: -1
        property alias tools: paneTools.data
        default property alias content: paneBody.data
        radius: Style.radiusLarge
        color: Style.wellBg
        border.color: Style.wellEdge
        clip: true

        Rectangle {
            id: paneHead
            x: 1
            y: 1
            width: parent.width - 2
            height: 26
            color: Style.wellHead
            Text {
                x: 10
                anchors.verticalCenter: parent.verticalCenter
                text: pane.slideIndex >= 0 ? qsTr("Slide %1").arg(pane.slideIndex + 1) : ""
                color: Style.textSoft
                font.pixelSize: Style.fontBody
                font.bold: true
            }
            Row {
                id: paneTools
                anchors.right: parent.right
                anchors.rightMargin: 4
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Item {
            id: paneBody
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: paneHead.bottom
            anchors.bottom: parent.bottom
            anchors.margins: 1
        }
    }

    // ---- Build: the slide's animations in order, each step's number on
    // the one that starts it; picked to edit on the Animations tab, dragged
    // to reorder, played over the stage
    Loader {
        id: buildSlot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: side.tabbed ? 8 : 0
        // Made the first time its tab shows, then kept.
        property bool kept: false
        active: side.tab === "build" || buildSlot.kept
        visible: side.tab === "build"
        onLoaded: buildSlot.kept = true
        sourceComponent: Component {
            SlidePane {
                id: buildPane
                objectName: "sideBuildPane"
                anchors.fill: parent
                slideIndex: side.dialog.edit ? side.dialog.edit.slides.index : -1
                readonly property var list: side.dialog.animationList || []
                readonly property var drag: side.dialog.buildDrag
                tools: [
                    AppButton {
                        objectName: "sideBuildPlay"
                        size: "compact"
                        ghost: true
                        enabled: buildPane.list.length > 0
                        iconSource: Icons.url(side.dialog.edit && side.dialog.edit.animations.playing ? "stop" : "play",
                                              Style.text)
                        onClicked: side.dialog.edit.animations.playing ? side.dialog.edit.animations.stop()
                                                                       : side.dialog.edit.animations.play()
                    }
                ]

                Flickable {
                    id: buildFlick
                    anchors.fill: parent
                    contentHeight: buildList.implicitHeight
                    clip: true
                    interactive: !buildPane.drag.active
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar {}
                    GroupList {
                        id: buildList
                        objectName: "sideBuildList"
                        width: buildFlick.width - Style.scrollGutter(buildFlick)
                        visible: buildPane.list.length > 0
                        Repeater {
                            model: buildPane.list
                            delegate: GroupRow {
                                id: buildRow
                                required property int index
                                required property var modelData
                                readonly property var picked: side.dialog.currentAnimation
                                selected: !!buildRow.picked && buildRow.picked.objectId === buildRow.modelData.objectId
                                          && buildRow.picked.order === buildRow.modelData.order
                                interactive: true
                                lifted: buildPane.drag.active && buildPane.drag.from === buildRow.index
                                dropAbove: buildPane.drag.active && buildPane.drag.insertBefore === buildRow.index
                                dropBelow: buildPane.drag.active && buildRow.index === buildPane.list.length - 1
                                           && buildPane.drag.insertBefore === buildPane.list.length
                                onClicked: side.dialog.selectAnimation(buildRow.modelData)
                                HandleTile {
                                    // The step a click starts; none for one that
                                    // goes with or after another.
                                    text: buildRow.modelData.step > 0 ? String(buildRow.modelData.step) : ""
                                    draggable: true
                                    ReorderMouseArea {
                                        anchors.fill: parent
                                        reorder: buildPane.drag
                                        rows: buildList.column
                                        index: buildRow.index
                                        label: String(buildRow.modelData.name)
                                    }
                                }
                                Column {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        width: parent.width
                                        text: String(buildRow.modelData.name)
                                        color: Style.textBright
                                        font.pixelSize: Style.fontBody
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: String(buildRow.modelData.summary)
                                        color: Style.textSoft
                                        font.pixelSize: Style.fontSmall
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
                    }
                    EmptyListNote {
                        width: buildFlick.width - 24
                        x: 12
                        visible: buildPane.list.length === 0
                        title: qsTr("Nothing builds on this slide")
                        hint: qsTr("Select an object, then pick an effect on the Animations tab.")
                    }
                }
            }
        }
    }

    // ---- Notes: the slide's speaker notes, saved with the slide
    Loader {
        id: notesSlot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: side.tabbed ? 8 : 0
        // Made the first time its tab shows, then kept.
        property bool kept: false
        active: side.tab === "notes" || notesSlot.kept
        visible: side.tab === "notes"
        onLoaded: notesSlot.kept = true
        sourceComponent: Component {
            SlidePane {
                id: notesPane
                objectName: "sideNotesPane"
                anchors.fill: parent
                readonly property SlideSlidesSession slides: side.dialog.edit ? side.dialog.edit.slides : null
                slideIndex: notesPane.slides ? notesPane.slides.index : -1

                Flickable {
                    id: notesFlick
                    anchors.fill: parent
                    contentHeight: notesEdit.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar {}
                    TextEdit {
                        id: notesEdit
                        objectName: "sideNotes"
                        width: notesFlick.width - Style.scrollGutter(notesFlick)
                        height: Math.max(implicitHeight, notesFlick.height)
                        padding: 8
                        color: Style.text
                        font.pixelSize: Style.fontBody
                        wrapMode: TextEdit.Wrap
                        selectByMouse: true
                        textFormat: TextEdit.PlainText
                        // The slide's notes, unless they are being typed: then the
                        // typing is the truth and is written through as it goes,
                        // one undo step for the whole sitting.
                        readonly property string stored: notesPane.slides ? String(notesPane.slides.notes || "") : ""
                        property bool typing: false
                        text: stored
                        onStoredChanged: if (!notesEdit.typing && notesEdit.text !== notesEdit.stored) notesEdit.text = notesEdit.stored
                        onTextChanged: {
                            if (!notesEdit.activeFocus || !notesPane.slides || notesEdit.text === notesEdit.stored)
                                return
                            notesEdit.typing = true
                            side.dialog.report(notesPane.slides.setNotes(notesEdit.text))
                            notesEdit.typing = false
                        }
                        onActiveFocusChanged: {
                            if (!side.dialog.edit)
                                return
                            if (notesEdit.activeFocus)
                                side.dialog.edit.objects.beginGesture()
                            else
                                side.dialog.edit.objects.endGesture()
                        }
                        Text {
                            visible: !notesEdit.text.length && !notesEdit.activeFocus
                            x: notesEdit.padding
                            y: notesEdit.padding
                            text: qsTr("Speaker notes for this slide")
                            color: Style.textCaption
                            font.pixelSize: Style.fontBody
                        }
                    }
                }
            }
        }
    }

    Flickable {
        id: flick
        objectName: "sideProperties"
        visible: side.tab === "properties"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        anchors.topMargin: side.tabbed ? 8 : 0
        contentHeight: column.implicitHeight + 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: column
            width: flick.width - Style.scrollGutter(flick)
            spacing: 6

            Text {
                visible: !side.has
                Layout.fillWidth: true
                Layout.topMargin: 24
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Nothing selected")
                color: Style.textCaption
                font.pixelSize: Style.fontBody
            }

            // ---- a theme's lyrics box: the active line's and the other
            // lines' types, where lines follow
            SidebarLineType {
                objectName: "sideActiveLineSection"
                visible: side.lyrics && side.following
                side: side
                line: "active_line"
                title: qsTr("Active line")
            }
            SidebarLineType {
                objectName: "sideOtherLinesSection"
                visible: side.lyrics && side.following
                side: side
                line: "other_lines"
                title: qsTr("Other lines")
            }

            // ---- a theme's lyrics box: the lines
            SidebarSection {
                visible: side.lyrics
                dialog: side.dialog
                sectionId: "lines"
                title: qsTr("Lines")
                SidebarRow {
                    label: qsTr("Follow lines")
                    SidebarNumber {
                        objectName: "sideFollowLines"
                        dialog: side.dialog
                        from: "setting"
                        prop: "follow_lines"
                    }
                }
                SidebarRow {
                    label: qsTr("Fluid slides")
                    SidebarToggle {
                        objectName: "sideFluid"
                        dialog: side.dialog
                        from: "setting"
                        prop: "fluid_slides"
                    }
                }
                SidebarRow {
                    visible: !!side.dialog.setting("fluid_slides")
                    label: qsTr("Verse gap")
                    SidebarNumber {
                        objectName: "sideVerseGap"
                        dialog: side.dialog
                        from: "setting"
                        prop: "verse_gap"
                        unit: "px"
                    }
                }
                SidebarRow {
                    label: qsTr("No mixed tops")
                    SidebarToggle {
                        objectName: "sideNoMixedTops"
                        dialog: side.dialog
                        from: "setting"
                        prop: "prevent_mixed_active"
                    }
                }
                SidebarRow {
                    visible: !side.following
                    label: qsTr("Balance wrapped lines")
                    SidebarToggle {
                        objectName: "sideBalance"
                        dialog: side.dialog
                        from: "setting"
                        prop: "balance_wrapped_lines"
                    }
                }
                SidebarRow {
                    visible: !side.following && !!side.dialog.setting("balance_wrapped_lines")
                    label: qsTr("Balance")
                    SidebarNumber {
                        objectName: "sideBalanceStrength"
                        dialog: side.dialog
                        from: "setting"
                        prop: "balance_wrapped_strength"
                        unit: "%"
                        step: 5
                    }
                }
            }

            // ---- chords (a linked box with them, or a theme's lyrics box)
            SidebarSection {
                visible: (side.type === "text" && !!side.sel.cue_linked && !!side.sel.show_chords) || side.lyrics
                dialog: side.dialog
                sectionId: "chords"
                title: qsTr("Chords")
                SidebarRow {
                    visible: side.lyrics
                    label: qsTr("Show chords")
                    SidebarToggle {
                        objectName: "sideShowChords"
                        dialog: side.dialog
                        prop: "show_chords"
                    }
                }
                SidebarRow {
                    label: qsTr("Size")
                    SidebarNumber {
                        objectName: "sideChordSize"
                        dialog: side.dialog
                        prop: "chord_size_pct"
                        unit: "%"
                        step: 5
                    }
                }
                SidebarRow {
                    label: qsTr("Gap")
                    SidebarNumber {
                        objectName: "sideChordGap"
                        dialog: side.dialog
                        prop: "chord_gap_pct"
                        unit: "%"
                        step: 5
                    }
                }
                SidebarRow {
                    label: qsTr("Color")
                    SidebarColor {
                        objectName: "sideChordColor"
                        dialog: side.dialog
                        prop: "chord_color"
                        icon: "lineColor"
                        dialogTitle: qsTr("Chord color")
                    }
                }
            }

            // ---- a theme's passage box: citations
            SidebarSection {
                visible: side.passage
                dialog: side.dialog
                sectionId: "citations"
                title: qsTr("Citations")
                SidebarRow {
                    label: qsTr("Verse numbers")
                    SidebarToggle {
                        objectName: "sideVerseNumbers"
                        dialog: side.dialog
                        prop: "verse_numbers"
                    }
                }
                SidebarRow {
                    visible: !!side.sel.verse_numbers
                    label: qsTr("Font")
                    SidebarChoice {
                        objectName: "sideVerseFont"
                        dialog: side.dialog
                        prop: "verse_font_family"
                        fieldWidth: 130
                        options: side.session ? side.session.fonts.families : []
                    }
                }
                SidebarRow {
                    visible: !!side.sel.verse_numbers
                    label: qsTr("Size")
                    SidebarNumber {
                        objectName: "sideVerseSize"
                        dialog: side.dialog
                        prop: "verse_font_size"
                    }
                }
                SidebarRow {
                    visible: !!side.sel.verse_numbers
                    label: qsTr("Style")
                    SidebarStyle {
                        objectName: "sideVerseStyle"
                        dialog: side.dialog
                        prefix: "verse_font_"
                        parts: ["bold", "italic"]
                    }
                }
                SidebarRow {
                    visible: !!side.sel.verse_numbers
                    label: qsTr("Color")
                    SidebarColor {
                        objectName: "sideVerseColor"
                        dialog: side.dialog
                        prop: "verse_color"
                        letter: "A"
                        dialogTitle: qsTr("Citation color")
                    }
                }
                SidebarRow {
                    visible: !!side.sel.verse_numbers
                    label: qsTr("Placing")
                    SidebarChoice {
                        objectName: "sideVerseValign"
                        dialog: side.dialog
                        prop: "verse_valign"
                        options: [
                            { value: "super", label: qsTr("Raised") },
                            { value: "middle", label: qsTr("Middle") },
                            { value: "baseline", label: qsTr("Baseline") }
                        ]
                    }
                }
                SidebarRow {
                    visible: !!side.sel.verse_numbers
                    label: qsTr("Gap")
                    SidebarNumber {
                        objectName: "sideVerseGapPct"
                        dialog: side.dialog
                        prop: "verse_margin_pct"
                        unit: "%"
                        step: 5
                    }
                }
                SidebarRow {
                    label: qsTr("Run on verses")
                    SidebarToggle {
                        objectName: "sideRunOnVerses"
                        dialog: side.dialog
                        from: "setting"
                        prop: "bible_fluid_verses"
                    }
                }
            }

            // ---- a shape's fill and line, or the frame behind a box's words
            SidebarSection {
                objectName: "sideFillSection"
                visible: side.fillable && !side.shapeOpen
                dialog: side.dialog
                sectionId: "fill"
                title: qsTr("Fill")
                SidebarRow {
                    id: fillKindRow
                    wide: true
                    label: ""
                    // None / Solid / Gradient / Picture: one strip, the row,
                    // its four parts even (a part sized to its label shifts
                    // when the chosen one's goes bold).
                    Item {
                        id: fillPick
                        objectName: "sideFillKind"
                        readonly property var kinds: ["none", "solid", "gradient", "picture"]
                        readonly property string value: String(side.sel.fill_kind)
                        signal picked(var value)
                        width: fillKindRow.width - 20
                        height: fillKindStrip.implicitHeight
                        SegmentedControl {
                            id: fillKindStrip
                            width: parent.width
                            stretch: true
                            options: [qsTr("None"), qsTr("Solid"), qsTr("Gradient"), qsTr("Picture")]
                            current: Math.max(0, fillPick.kinds.indexOf(fillPick.value))
                            onChosen: function (index) { fillPick.picked(fillPick.kinds[index]) }
                        }
                        onPicked: function (v) {
                            // A picture fill without a picture yet asks for one.
                            if (v === "picture" && !String(side.sel.fill_picture_file || "").length)
                                side.dialog.mediaFileAsked("fill")
                            else
                                side.setProp("fill_kind", v)
                        }
                    }
                }
                // A gradient: its kind, its angle, the colours at its ends.
                SidebarRow {
                    visible: side.fillKind === "gradient"
                    label: qsTr("Gradient")
                    SidebarChoice {
                        objectName: "sideGradientKind"
                        dialog: side.dialog
                        prop: "gradient_kind"
                        options: [
                            { value: "linear", label: qsTr("Linear") },
                            { value: "radial", label: qsTr("Radial") }
                        ]
                    }
                }
                SidebarRow {
                    visible: side.fillKind === "gradient" && String(side.sel.gradient_kind) !== "radial"
                    label: qsTr("Angle")
                    SidebarNumber {
                        objectName: "sideGradientAngle"
                        dialog: side.dialog
                        prop: "gradient_angle"
                        unit: "°"
                        step: 15
                    }
                }
                SidebarRow {
                    visible: side.fillKind === "gradient" && String(side.sel.gradient_kind) === "radial"
                    label: qsTr("Center")
                    SidebarNumber {
                        objectName: "sideGradientCenterX"
                        dialog: side.dialog
                        prop: "gradient_center_x"
                        percent: true
                        label: "X"
                        fieldWidth: 56
                        unit: "%"
                        step: 5
                    }
                    SidebarNumber {
                        objectName: "sideGradientCenterY"
                        dialog: side.dialog
                        prop: "gradient_center_y"
                        percent: true
                        label: "Y"
                        fieldWidth: 56
                        unit: "%"
                        step: 5
                    }
                }
                SidebarRow {
                    visible: side.fillKind === "gradient"
                    label: qsTr("From")
                    SidebarColor {
                        objectName: "sideGradientFrom"
                        dialog: side.dialog
                        prop: "gradient_from"
                        alpha: true
                        dialogTitle: qsTr("Gradient start")
                    }
                }
                SidebarRow {
                    visible: side.fillKind === "gradient"
                    label: qsTr("To")
                    SidebarColor {
                        objectName: "sideGradientTo"
                        dialog: side.dialog
                        prop: "gradient_to"
                        alpha: true
                        dialogTitle: qsTr("Gradient end")
                    }
                }
                SidebarRow {
                    visible: side.fillKind === "solid"
                    label: qsTr("Color")
                    SidebarColor {
                        objectName: "sideFill"
                        dialog: side.dialog
                        prop: "fill"
                        alpha: true
                        dialogTitle: qsTr("Fill color")
                    }
                }
                SidebarRow {
                    visible: side.fillKind === "picture"
                    label: String(side.sel.fill_picture_file || qsTr("No picture"))
                    AppButton {
                        objectName: "sideFillPictureChange"
                        size: "compact"
                        label: qsTr("Change…")
                        onClicked: side.dialog.mediaFileAsked("fill")
                    }
                }
                SidebarRow {
                    visible: side.fillKind === "picture"
                    label: qsTr("Fit")
                    SidebarChoice {
                        objectName: "sideFillPictureFit"
                        dialog: side.dialog
                        prop: "fill_picture_fit"
                        options: side.fits
                    }
                }
            }
            SidebarSection {
                objectName: "sideLineSection"
                visible: side.fillable
                dialog: side.dialog
                sectionId: "line"
                title: qsTr("Line")
                SidebarRow {
                    label: qsTr("Color")
                    SidebarColor {
                        objectName: "sideStroke"
                        dialog: side.dialog
                        prop: "stroke"
                        icon: "lineColor"
                        alpha: true
                        dialogTitle: qsTr("Line color")
                    }
                }
                SidebarRow {
                    label: qsTr("Width")
                    SidebarNumber {
                        objectName: "sideStrokeWidth"
                        dialog: side.dialog
                        prop: "stroke_width"
                        unit: "px"
                    }
                }
                SidebarRow {
                    label: qsTr("Dash")
                    SidebarChoice {
                        objectName: "sideLineDash"
                        dialog: side.dialog
                        prop: "line_dash"
                        options: [
                            { value: "solid", label: qsTr("Solid") },
                            { value: "dash", label: qsTr("Dashed") },
                            { value: "dot", label: qsTr("Dotted") },
                            { value: "dash_dot", label: qsTr("Dash-dot") }
                        ]
                    }
                }
                // An open outline's ends.
                SidebarRow {
                    visible: side.shapeOpen
                    label: qsTr("Start")
                    SidebarChoice {
                        objectName: "sideLineStart"
                        dialog: side.dialog
                        prop: "line_start"
                        options: side.lineEnds
                    }
                }
                SidebarRow {
                    visible: side.shapeOpen
                    label: qsTr("End")
                    SidebarChoice {
                        objectName: "sideLineEnd"
                        dialog: side.dialog
                        prop: "line_end"
                        options: side.lineEnds
                    }
                }
            }
            // ---- effects: round the shape, frame or picture; on the words
            SidebarSection {
                objectName: "sideEffectsSection"
                visible: side.effectable
                dialog: side.dialog
                sectionId: "effects"
                title: qsTr("Effects")
                SidebarRow {
                    label: qsTr("Shadow")
                    SidebarToggle {
                        objectName: "sideShadowOn"
                        dialog: side.dialog
                        prop: "shadow_on"
                    }
                }
                SidebarRow {
                    visible: !!side.sel.shadow_on
                    label: qsTr("Color")
                    SidebarColor {
                        objectName: "sideShadowColor"
                        dialog: side.dialog
                        prop: "shadow_color"
                        alpha: true
                        dialogTitle: qsTr("Shadow color")
                    }
                }
                SidebarRow {
                    visible: !!side.sel.shadow_on
                    label: qsTr("Blur")
                    SidebarNumber {
                        objectName: "sideShadowBlur"
                        dialog: side.dialog
                        prop: "shadow_blur"
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: !!side.sel.shadow_on
                    label: qsTr("Distance")
                    SidebarNumber {
                        objectName: "sideShadowDistance"
                        dialog: side.dialog
                        prop: "shadow_distance"
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: !!side.sel.shadow_on
                    label: qsTr("Angle")
                    SidebarNumber {
                        objectName: "sideShadowAngle"
                        dialog: side.dialog
                        prop: "shadow_angle"
                        unit: "°"
                        step: 15
                    }
                }
                SidebarRow {
                    label: qsTr("Glow")
                    SidebarToggle {
                        objectName: "sideGlowOn"
                        dialog: side.dialog
                        prop: "glow_on"
                    }
                }
                SidebarRow {
                    visible: !!side.sel.glow_on
                    label: qsTr("Color")
                    SidebarColor {
                        objectName: "sideGlowColor"
                        dialog: side.dialog
                        prop: "glow_color"
                        alpha: true
                        dialogTitle: qsTr("Glow color")
                    }
                }
                SidebarRow {
                    visible: !!side.sel.glow_on
                    label: qsTr("Size")
                    SidebarNumber {
                        objectName: "sideGlowSize"
                        dialog: side.dialog
                        prop: "glow_size"
                        unit: "px"
                    }
                }
                SidebarRow {
                    label: qsTr("Soft edge")
                    SidebarNumber {
                        objectName: "sideSoftEdge"
                        dialog: side.dialog
                        prop: "soft_edge"
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: side.textLike
                    label: qsTr("Text outline")
                    SidebarNumber {
                        objectName: "sideTextOutlineWidth"
                        dialog: side.dialog
                        prop: "text_outline_width"
                        unit: "px"
                        isFloat: true
                        step: 0.5
                    }
                }
                SidebarRow {
                    visible: side.textLike && Number(side.sel.text_outline_width) > 0
                    label: qsTr("Outline color")
                    SidebarColor {
                        objectName: "sideTextOutlineColor"
                        dialog: side.dialog
                        prop: "text_outline_color"
                        letter: "A"
                        dialogTitle: qsTr("Text outline color")
                    }
                }
                SidebarRow {
                    visible: side.textLike
                    label: qsTr("Text shadow")
                    SidebarToggle {
                        objectName: "sideTextShadowOn"
                        dialog: side.dialog
                        prop: "text_shadow_on"
                    }
                }
                SidebarRow {
                    visible: side.textLike && !!side.sel.text_shadow_on
                    label: qsTr("Color")
                    SidebarColor {
                        objectName: "sideTextShadowColor"
                        dialog: side.dialog
                        prop: "text_shadow_color"
                        letter: "A"
                        alpha: true
                        dialogTitle: qsTr("Text shadow color")
                    }
                }
                SidebarRow {
                    visible: side.textLike && !!side.sel.text_shadow_on
                    label: qsTr("Blur")
                    SidebarNumber {
                        objectName: "sideTextShadowBlur"
                        dialog: side.dialog
                        prop: "text_shadow_blur"
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: side.textLike && !!side.sel.text_shadow_on
                    label: qsTr("Distance")
                    SidebarNumber {
                        objectName: "sideTextShadowDistance"
                        dialog: side.dialog
                        prop: "text_shadow_distance"
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: side.textLike && !!side.sel.text_shadow_on
                    label: qsTr("Angle")
                    SidebarNumber {
                        objectName: "sideTextShadowAngle"
                        dialog: side.dialog
                        prop: "text_shadow_angle"
                        unit: "°"
                        step: 15
                    }
                }
            }

            // ---- a shape
            SidebarSection {
                visible: side.has && side.type === "shape"
                dialog: side.dialog
                sectionId: "shape"
                title: qsTr("Shape")
                SidebarRow {
                    visible: side.sel.shape_kind !== "drawn"
                    label: qsTr("Kind")
                    SidebarChoice {
                        objectName: "sideShapeKind"
                        dialog: side.dialog
                        prop: "shape_kind"
                        options: [
                            { value: "rectangle", label: qsTr("Rectangle") },
                            { value: "rounded_rect", label: qsTr("Rounded rectangle") },
                            { value: "ellipse", label: qsTr("Ellipse") },
                            { value: "triangle", label: qsTr("Triangle") },
                            { value: "right_triangle", label: qsTr("Right triangle") },
                            { value: "rhombus", label: qsTr("Rhombus") },
                            { value: "polygon", label: qsTr("Polygon") },
                            { value: "star", label: qsTr("Star") },
                            { value: "arrow", label: qsTr("Arrow") },
                            { value: "double_arrow", label: qsTr("Double arrow") },
                            { value: "cloud", label: qsTr("Cloud callout") }
                        ]
                    }
                }
                // A drawn shape is its points; it may join up and fill.
                SidebarRow {
                    visible: side.sel.shape_kind === "drawn"
                    label: qsTr("Closed")
                    SidebarToggle {
                        objectName: "sideShapeClosed"
                        dialog: side.dialog
                        prop: "shape_closed"
                        enabled: (side.sel.shape_path || []).length >= 3
                    }
                }
                SidebarRow {
                    visible: side.sel.shape_kind === "rounded_rect"
                    label: qsTr("Corner")
                    SidebarNumber {
                        objectName: "sideShapeCorner"
                        dialog: side.dialog
                        prop: "shape_corner"
                        isFloat: true
                        step: 0.02
                    }
                }
                SidebarRow {
                    visible: side.sel.shape_kind === "polygon" || side.sel.shape_kind === "star"
                    label: side.sel.shape_kind === "star" ? qsTr("Points") : qsTr("Sides")
                    SidebarNumber {
                        objectName: "sideShapePoints"
                        dialog: side.dialog
                        prop: "shape_points"
                    }
                }
                SidebarRow {
                    visible: side.sel.shape_kind === "star"
                    label: qsTr("Inner radius")
                    SidebarNumber {
                        objectName: "sideShapeInner"
                        dialog: side.dialog
                        prop: "shape_inner"
                        isFloat: true
                        step: 0.02
                    }
                }
                SidebarRow {
                    visible: side.arrow
                    label: qsTr("Head")
                    SidebarNumber {
                        objectName: "sideShapeHead"
                        dialog: side.dialog
                        prop: "shape_head"
                        isFloat: true
                        step: 0.02
                    }
                }
                SidebarRow {
                    visible: side.arrow
                    label: qsTr("Shaft")
                    SidebarNumber {
                        objectName: "sideShapeThickness"
                        dialog: side.dialog
                        prop: "shape_thickness"
                        isFloat: true
                        step: 0.02
                    }
                }
            }

            // ---- a picture
            SidebarSection {
                visible: side.has && side.type === "image" && side.dialog.selSupports("fit")
                dialog: side.dialog
                sectionId: "picture"
                title: qsTr("Picture")
                SidebarRow {
                    label: qsTr("Fit")
                    SidebarChoice {
                        objectName: "sideObjectFit"
                        dialog: side.dialog
                        prop: "object_fit"
                        options: side.fits
                    }
                }
                SidebarRow {
                    visible: side.binding === "static"
                    label: side.sel.filename ? side.dialog.mediaName(false, side.sel.filename) : qsTr("No picture")
                    AppButton {
                        objectName: "sidePictureChange"
                        size: "compact"
                        label: qsTr("Change…")
                        onClicked: side.dialog.mediaFileAsked("element")
                    }
                }
                // The crop: what is cut off each edge, as a share of the picture.
                SidebarRow {
                    visible: side.binding === "static"
                    label: qsTr("Crop")
                    SidebarNumber {
                        objectName: "sideCropLeft"
                        dialog: side.dialog
                        prop: "crop_left"
                        percent: true
                        label: "L"
                        fieldWidth: 56
                        unit: "%"
                        step: 5
                    }
                    SidebarNumber {
                        objectName: "sideCropRight"
                        dialog: side.dialog
                        prop: "crop_right"
                        percent: true
                        label: "R"
                        fieldWidth: 56
                        unit: "%"
                        step: 5
                    }
                }
                SidebarRow {
                    visible: side.binding === "static"
                    label: ""
                    SidebarNumber {
                        objectName: "sideCropTop"
                        dialog: side.dialog
                        prop: "crop_top"
                        percent: true
                        label: "T"
                        fieldWidth: 56
                        unit: "%"
                        step: 5
                    }
                    SidebarNumber {
                        objectName: "sideCropBottom"
                        dialog: side.dialog
                        prop: "crop_bottom"
                        percent: true
                        label: "B"
                        fieldWidth: 56
                        unit: "%"
                        step: 5
                    }
                }
                SidebarRow {
                    visible: side.binding === "static"
                    label: qsTr("Recolor")
                    SidebarChoice {
                        objectName: "sideRecolor"
                        dialog: side.dialog
                        prop: "recolor"
                        options: [
                            { value: "none", label: qsTr("None") },
                            { value: "grayscale", label: qsTr("Grayscale") },
                            { value: "tint", label: qsTr("Tint") }
                        ]
                    }
                }
                SidebarRow {
                    visible: side.binding === "static" && String(side.sel.recolor) === "tint"
                    label: qsTr("Tint color")
                    SidebarColor {
                        objectName: "sideTint"
                        dialog: side.dialog
                        prop: "tint"
                        dialogTitle: qsTr("Tint color")
                    }
                }
                SidebarRow {
                    visible: side.binding === "static"
                    label: qsTr("Transparency")
                    SidebarNumber {
                        objectName: "sidePictureTransparency"
                        dialog: side.dialog
                        prop: "picture_transparency"
                        percent: true
                        unit: "%"
                        step: 5
                    }
                }
            }

            // ---- a table: its size, its look, and its cells' words
            SidebarSection {
                visible: side.has && side.type === "table"
                dialog: side.dialog
                sectionId: "table"
                title: qsTr("Table")
                SidebarRow {
                    label: qsTr("Size")
                    SidebarNumber {
                        objectName: "sideTableRows"
                        dialog: side.dialog
                        prop: "table_rows"
                        label: qsTr("Rows")
                        fieldWidth: 56
                    }
                    SidebarNumber {
                        objectName: "sideTableColumns"
                        dialog: side.dialog
                        prop: "table_columns"
                        label: qsTr("Cols")
                        fieldWidth: 56
                    }
                }
                SidebarRow {
                    label: qsTr("Header row")
                    SidebarToggle {
                        objectName: "sideTableHeader"
                        dialog: side.dialog
                        prop: "table_header"
                    }
                }
                SidebarRow {
                    label: qsTr("Banded rows")
                    SidebarToggle {
                        objectName: "sideTableBanding"
                        dialog: side.dialog
                        prop: "table_banding"
                    }
                }
                SidebarRow {
                    label: qsTr("Borders")
                    SidebarColor {
                        objectName: "sideTableBorderColor"
                        dialog: side.dialog
                        prop: "table_border_color"
                        icon: "lineColor"
                        alpha: true
                        dialogTitle: qsTr("Border color")
                    }
                    SidebarNumber {
                        objectName: "sideTableBorderWidth"
                        dialog: side.dialog
                        prop: "table_border_width"
                        fieldWidth: 64
                        unit: "px"
                    }
                }
                // The cells, row by row, each written when it is left. The
                // model is a count, so a commit keeps every field (and where
                // the operator clicked next); wide tables scroll sideways.
                Flickable {
                    id: cellScroll
                    width: parent.width
                    implicitHeight: cellGrid.height + 10
                    contentWidth: cellGrid.width + 20
                    contentHeight: cellGrid.height
                    flickableDirection: Flickable.HorizontalFlick
                    interactive: contentWidth > width
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    Grid {
                        id: cellGrid
                        objectName: "sideTableCells"
                        readonly property int cols: Math.max(1, Number(side.sel.table_columns))
                        readonly property real cellW: Math.max(64, (cellScroll.width - 20 - (cellGrid.cols - 1) * cellGrid.spacing)
                                                                   / cellGrid.cols)
                        x: 10
                        columns: cellGrid.cols
                        spacing: 4
                        Repeater {
                            model: side.has && side.type === "table"
                                   ? Number(side.sel.table_rows) * Number(side.sel.table_columns) : 0
                            delegate: DialogField {
                                id: cellField
                                required property int index
                                width: cellGrid.cellW
                                text: String((side.sel.table_cells || [])[cellField.index] || "")
                                onCommitted: function (value) {
                                    const cells = (side.sel.table_cells || []).slice()
                                    if (cells[cellField.index] === value)
                                        return
                                    cells[cellField.index] = value
                                    side.setProp("table_cells", cells)
                                }
                            }
                        }
                    }
                }
            }

            // ---- a video: when it plays, and what shows until it does
            SidebarSection {
                visible: side.has && side.type === "video"
                dialog: side.dialog
                sectionId: "video"
                title: qsTr("Video")
                SidebarRow {
                    label: side.sel.filename ? side.dialog.mediaName(true, side.sel.filename) : qsTr("No video")
                    AppButton {
                        objectName: "sideVideoChange"
                        size: "compact"
                        label: qsTr("Change…")
                        onClicked: side.dialog.mediaFileAsked("video_file")
                    }
                }
                SidebarRow {
                    label: qsTr("Fit")
                    SidebarChoice {
                        objectName: "sideVideoFit"
                        dialog: side.dialog
                        prop: "object_fit"
                        options: side.fits
                    }
                }
                SidebarRow {
                    label: qsTr("Start")
                    SidebarChoice {
                        objectName: "sideVideoStart"
                        dialog: side.dialog
                        prop: "video_start"
                        options: [
                            { value: "on_show", label: qsTr("On show") },
                            { value: "on_click", label: qsTr("On click") }
                        ]
                    }
                }
                SidebarRow {
                    label: qsTr("Loop")
                    SidebarToggle {
                        objectName: "sideVideoLoop"
                        dialog: side.dialog
                        prop: "video_loop"
                    }
                }
                SidebarRow {
                    label: qsTr("Mute")
                    SidebarToggle {
                        objectName: "sideVideoMute"
                        dialog: side.dialog
                        prop: "video_mute"
                    }
                }
                // Shown until it plays: on a click's wait, and in thumbnails.
                SidebarRow {
                    label: String(side.sel.video_poster || qsTr("No poster"))
                    AppButton {
                        objectName: "sideVideoPoster"
                        size: "compact"
                        label: qsTr("Choose…")
                        onClicked: side.dialog.mediaFileAsked("poster")
                    }
                    AppButton {
                        objectName: "sideVideoPosterClear"
                        visible: String(side.sel.video_poster || "").length > 0
                        size: "compact"
                        ghost: true
                        label: qsTr("Clear")
                        onClicked: side.setProp("video_poster", "")
                    }
                }
            }

            // ---- text options
            SidebarSection {
                visible: side.textLike && side.type !== "table"
                dialog: side.dialog
                sectionId: "text"
                title: qsTr("Text options")
                SidebarRow {
                    visible: side.runs
                    label: qsTr("Line spacing")
                    SidebarNumber {
                        objectName: "sideLineHeight"
                        dialog: side.dialog
                        from: "paragraph"
                        prop: "line_height"
                        isFloat: true
                        step: 0.05
                    }
                }
                SidebarRow {
                    visible: side.runs
                    label: qsTr("Tracking")
                    SidebarNumber {
                        objectName: "sideLetterSpacing"
                        dialog: side.dialog
                        prop: "letter_spacing"
                        isFloat: true
                        step: 0.5
                    }
                }
                SidebarRow {
                    visible: side.runs
                    label: qsTr("Line gap")
                    SidebarNumber {
                        objectName: "sideLineGap"
                        dialog: side.dialog
                        prop: "line_gap"
                        unit: "px"
                    }
                }
                // Paragraphs: a text box's own; a theme's body box takes the margins.
                SidebarRow {
                    visible: side.staticText
                    label: qsTr("Space before")
                    SidebarNumber {
                        objectName: "sideSpaceBefore"
                        dialog: side.dialog
                        from: "paragraph"
                        prop: "space_before"
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: side.staticText
                    label: qsTr("Space after")
                    SidebarNumber {
                        objectName: "sideSpaceAfter"
                        dialog: side.dialog
                        from: "paragraph"
                        prop: "space_after"
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: side.staticText
                    label: qsTr("First-line indent")
                    SidebarNumber {
                        objectName: "sideIndent"
                        dialog: side.dialog
                        from: "paragraph"
                        prop: "indent"
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: side.staticText
                    label: qsTr("Shrink text to fit")
                    ToggleSwitch {
                        objectName: "sideAutofit"
                        checked: side.sel.autofit === "shrink"
                        onToggled: side.setProp("autofit", side.sel.autofit === "shrink" ? "none" : "shrink")
                    }
                }
                // The words set in columns, side by side, each filled in turn.
                SidebarRow {
                    visible: side.staticText && !side.sel.cue_linked
                    label: qsTr("Columns")
                    SidebarNumber {
                        objectName: "sideColumns"
                        dialog: side.dialog
                        prop: "columns"
                        fieldWidth: 56
                    }
                    SidebarNumber {
                        objectName: "sideColumnGap"
                        dialog: side.dialog
                        prop: "column_gap"
                        visible: Number(side.sel.columns) > 1
                        label: qsTr("Gap")
                        fieldWidth: 64
                        unit: "px"
                    }
                }
                SidebarRow {
                    visible: side.type === "text"
                    label: qsTr("Margins")
                    SidebarNumber {
                        objectName: "sideMarginLeft"
                        dialog: side.dialog
                        prop: "margin_left"
                        label: "L"
                        fieldWidth: 56
                    }
                    SidebarNumber {
                        objectName: "sideMarginRight"
                        dialog: side.dialog
                        prop: "margin_right"
                        label: "R"
                        fieldWidth: 56
                    }
                }
                SidebarRow {
                    visible: side.type === "text"
                    label: ""
                    SidebarNumber {
                        objectName: "sideMarginTop"
                        dialog: side.dialog
                        prop: "margin_top"
                        label: "T"
                        fieldWidth: 56
                    }
                    SidebarNumber {
                        objectName: "sideMarginBottom"
                        dialog: side.dialog
                        prop: "margin_bottom"
                        label: "B"
                        fieldWidth: 56
                    }
                }
                SidebarRow {
                    visible: side.linkedText
                    label: qsTr("Upcoming lines color")
                    SidebarColor {
                        objectName: "sideUpcomingColor"
                        dialog: side.dialog
                        prop: "upcoming_color"
                        letter: "A"
                        dialogTitle: qsTr("Upcoming lines color")
                    }
                }
                SidebarRow {
                    visible: side.linkedText
                    label: qsTr("Upcoming lines size")
                    SidebarNumber {
                        objectName: "sideUpcomingSize"
                        dialog: side.dialog
                        prop: "upcoming_font_size"
                    }
                }
                SidebarRow {
                    visible: side.linkedText
                    label: qsTr("Upcoming lines style")
                    SidebarStyle {
                        objectName: "sideUpcomingStyle"
                        dialog: side.dialog
                        prefix: "upcoming_"
                    }
                }
                SidebarRow {
                    visible: side.type === "indicator"
                    label: qsTr("With no verse")
                    SidebarChoice {
                        objectName: "sideIndicatorFallback"
                        dialog: side.dialog
                        prop: "indicator_fallback"
                        options: [
                            { value: "slide", label: qsTr("Slide number") },
                            { value: "blank", label: qsTr("Nothing") }
                        ]
                    }
                }
                SidebarRow {
                    visible: side.type === "indicator"
                    label: qsTr("Gap")
                    SidebarNumber {
                        objectName: "sideIndicatorGap"
                        dialog: side.dialog
                        prop: "indicator_gap"
                        isFloat: true
                        step: 0.1
                    }
                }
                SidebarRow {
                    visible: side.dialog.selSupports("clock")
                    label: qsTr("24-hour")
                    SidebarToggle {
                        objectName: "sideClock24h"
                        dialog: side.dialog
                        prop: "clock_24h"
                    }
                }
                SidebarRow {
                    visible: side.dialog.selSupports("clock")
                    label: qsTr("Seconds")
                    SidebarToggle {
                        objectName: "sideClockSeconds"
                        dialog: side.dialog
                        prop: "clock_seconds"
                    }
                }
            }

            // ---- a theme element: which slides
            SidebarSection {
                visible: side.has && side.dialog.isTextTheme
                dialog: side.dialog
                sectionId: "slides"
                title: qsTr("Shown on")
                SidebarRow {
                    label: qsTr("Slides")
                    SidebarChoice {
                        objectName: "sideRule"
                        dialog: side.dialog
                        prop: "rule"
                        fieldWidth: 130
                        options: [
                            { value: "all", label: qsTr("Every slide") },
                            { value: "first", label: qsTr("The first N") },
                            { value: "last", label: qsTr("The last N") },
                            { value: "except_first", label: qsTr("All but the first N") },
                            { value: "except_last", label: qsTr("All but the last N") }
                        ]
                    }
                }
                SidebarRow {
                    visible: String(side.sel.rule) !== "all"
                    label: qsTr("N")
                    SidebarNumber {
                        objectName: "sideRuleCount"
                        dialog: side.dialog
                        prop: "count"
                    }
                }
                SidebarRow {
                    visible: side.dialog.edit && side.dialog.edit.context === "song"
                    label: qsTr("Only when")
                    SidebarChoice {
                        objectName: "sideShowWhen"
                        dialog: side.dialog
                        prop: "show_when"
                        fieldWidth: 150
                        options: [
                            { value: "always", label: qsTr("Always") },
                            { value: "song_copyright", label: qsTr("The song shows copyright") }
                        ]
                    }
                }
            }

            // ---- size and position
            SidebarSection {
                visible: side.has
                dialog: side.dialog
                sectionId: "geometry"
                title: qsTr("Size and position")
                SidebarRow {
                    label: qsTr("Position")
                    SidebarNumber {
                        objectName: "sideX"
                        dialog: side.dialog
                        prop: "x"
                        label: "X"
                        fieldWidth: 64
                    }
                    SidebarNumber {
                        objectName: "sideY"
                        dialog: side.dialog
                        prop: "y"
                        label: "Y"
                        fieldWidth: 64
                    }
                }
                SidebarRow {
                    label: qsTr("Size")
                    SidebarNumber {
                        objectName: "sideWidth"
                        dialog: side.dialog
                        prop: "width"
                        label: "W"
                        fieldWidth: 64
                    }
                    SidebarNumber {
                        objectName: "sideHeight"
                        dialog: side.dialog
                        prop: "height"
                        label: "H"
                        fieldWidth: 64
                    }
                }
                SidebarRow {
                    label: qsTr("Rotation")
                    SidebarNumber {
                        objectName: "sideRotation"
                        dialog: side.dialog
                        prop: "rotation"
                        unit: "°"
                    }
                }
                SidebarRow {
                    label: qsTr("Opacity")
                    SidebarNumber {
                        objectName: "sideOpacity"
                        dialog: side.dialog
                        prop: "opacity"
                        percent: true
                        unit: "%"
                        step: 5
                    }
                }
            }
        }
    }
}

