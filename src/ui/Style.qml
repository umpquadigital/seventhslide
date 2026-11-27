pragma Singleton
import QtQuick
// For ScrollBar, which scrollGutter reads as an attached property; qmllint
// does not see a use inside a function.
import QtQuick.Controls // qmllint disable unused-imports

QtObject {
    id: style
    // Dark console palette.
    readonly property color pageBg: "#0d0d0d"
    readonly property color panelBg: "#141414"
    readonly property color border: "#252525"
    readonly property color accent: "#004791"
    /// The outline round a box on the slide editor's stage (StageBox).
    readonly property color lyricsOutline: "#486b90"
    readonly property color text: "#d4d4d4"
    readonly property color textMuted: style.inkGrey54
    readonly property color textOnAccent: "#ffffff"

    /// The outline of the slide editor and of every window (``windowEdge``).
    readonly property color modalEdge: "#3a3a3a"

    /// Type scale: every UI text size is one of these, smallest first.
    /// Glyph-sized labels: stepper arrows, menu chevrons, counts, slide numbers.
    readonly property int fontMicro: 9
    /// Field captions, tags, hints.
    readonly property int fontCaption: 10
    /// Secondary lines, compact buttons, ribbon controls.
    readonly property int fontSmall: 11
    /// List rows, fields and body text.
    readonly property int fontBody: 12
    readonly property int fontUi: 13
    readonly property int fontTitle: 14
    /// Settings sheet titles.
    readonly property int fontHeading: 15
    readonly property int pad: 10
    // Corner radii: 3 for controls and rows (radiusSmall), 5 for anything that
    // holds them - panels, cards, wells, dialog boxes, islands (radius and
    // radiusLarge, kept as two names for the places that use each).
    // Hairline marks (0-2.5 px) stay literal, and a pill's ends are half its
    // height (``radius: height / 2``).
    readonly property int radiusSmall: 3
    readonly property int radius: 5
    readonly property int radiusLarge: 5

    /// Shared operator button family (island toolbar + whisper of depth).
    readonly property int btnRadius: radiusSmall
    readonly property int btnCompactH: 24
    readonly property int btnStandardH: 28
    readonly property int btnCommandH: 36
    readonly property int btnCompactFont: style.fontSmall
    readonly property int btnStandardFont: style.fontBody
    readonly property int btnCommandFont: style.fontUi
    readonly property int btnCompactIcon: 12
    readonly property int btnStandardIcon: 14
    readonly property int btnCommandIcon: 16

    /// The ribbon's controls: their height, glyph, label and padding.
    readonly property int ribbonH: 28
    readonly property int ribbonIcon: 14
    readonly property int ribbonFont: style.fontBody
    /// The quiet name before a field (Duration, Start).
    readonly property int ribbonCaption: style.fontBody
    readonly property int ribbonPad: 10
    /// A split button's caret, and a number field's + and -.
    readonly property int ribbonSplitW: 18
    readonly property int ribbonStepW: 16
    /// Between the two rows, and the ribbon's margin above and below them.
    readonly property int ribbonRowGap: 6
    readonly property int ribbonEdge: 9
    /// Between controls in a row, and a group's inset from its hairline.
    readonly property int ribbonSpacing: 5
    readonly property int ribbonGroupPad: 13
    /// Both rows and the gap: a gallery's or palette's height.
    readonly property int ribbonTallH: 2 * style.ribbonH + style.ribbonRowGap
    /// Hairline blends with the surface: ~#3a3a3a on island toolbars, quieter
    /// on dark panel headers (Auto / Settings / Add) so it is not a hard outline.
    readonly property color btnEdge: Qt.rgba(1, 1, 1, 0.10)
    readonly property color btnEdgeHover: Qt.rgba(1, 1, 1, 0.20)
    readonly property color btnNeutralTop: "#252525"
    readonly property color btnNeutralBot: "#202020"
    readonly property color btnNeutralHoverTop: "#2c2c2c"
    readonly property color btnNeutralHoverBot: "#282828"
    readonly property color btnNeutralPressTop: "#1f1f1f"
    readonly property color btnNeutralPressBot: "#1c1c1c"
    /// Inset ghost face - recedes on operator panels and the lifted settings sheet.
    readonly property color btnGhost: "#101010"
    readonly property color btnGhostHover: "#191919"
    readonly property color btnGhostPress: "#0c0c0c"
    readonly property color btnDangerFill: "#1c1616"
    readonly property color btnDangerHover: "#2a1818"
    readonly property color btnDangerPress: "#181010"
    readonly property color btnDangerEdge: "#5a3030"
    readonly property color btnDangerEdgeHover: "#803030"
    readonly property color btnDangerInk: "#c07070"
    readonly property color btnDangerInkHover: "#d88888"
    readonly property color btnPrimaryTop: "#05509c"
    readonly property color btnPrimaryBot: "#004791"
    readonly property color btnPrimaryHoverTop: "#0a5cad"
    readonly property color btnPrimaryHoverBot: "#0055a8"
    readonly property color btnPrimaryPressTop: "#003f82"
    readonly property color btnPrimaryPressBot: "#003a75"
    readonly property color btnPrimaryEdge: "#00356e"
    readonly property color btnPrimaryEdgeHover: "#002a58"
    readonly property color btnBlankOnTop: "#c94444"
    readonly property color btnBlankOnBot: "#a83030"
    readonly property color btnBlankOnHoverTop: "#d65a5a"
    readonly property color btnBlankOnHoverBot: "#b83a3a"
    readonly property color btnBlankOnEdge: "#7a1f1f"
    readonly property color btnFreezeOnTop: "#2f92c4"
    readonly property color btnFreezeOnBot: "#1f6f9c"
    readonly property color btnFreezeOnHoverTop: "#3aa0d0"
    readonly property color btnFreezeOnHoverBot: "#2680b0"
    readonly property color btnFreezeOnEdge: "#175572"
    /// The slide editor's largest size; a smaller window fits it to itself.
    readonly property int editorShellWidth: 1320
    readonly property int editorShellHeight: 880
    /// Its height follows the slide's; it keeps at least this for the sidebar.
    readonly property int editorShellMinHeight: 620
    /// The Settings window.
    readonly property int settingsWindowW: 758
    readonly property int settingsWindowH: 584
    /// The Services window: its table, with no rail (tags filter from the
    /// search field's menu).
    readonly property int servicesWindowW: 620
    /// The Output window while it browses (its theme designer is larger).
    readonly property int outputWindowW: 880
    readonly property int outputWindowH: 640
    /// The Collaboration window: no rail, so its page is about as wide as
    /// Settings' pages are; as tall as the Output window, for the messages.
    readonly property int collaborationWindowW: 620
    readonly property int collaborationWindowH: 640

    /// Settings-dialog surfaces (Output / App Settings / Song / Services).
    /// Lifted a few steps above the #0d0d0d dimmer so the sheet reads as a card.
    readonly property color dialogBg: "#161616"
    readonly property color dialogChrome: "#1c1c1c"
    /// Freeform / text-theme / background-theme design shells.
    readonly property color editorBarBg: "#181818"
    /// Round a colour bar on a swatch button.
    readonly property color swatchRing: Qt.rgba(1, 1, 1, 0.15)
    /// A gallery card's sketch: what shows before, and what comes.
    readonly property color galleryBase: "#2a2a2a"
    readonly property color galleryShade: "#666666"
    readonly property color galleryObject: "#9a9a9a"
    readonly property color dialogField: "#101010"
    readonly property color dialogEdge: "#303030"
    readonly property color dialogRow: "#1d1d1d"
    readonly property color dialogRowHover: "#272727"

    /// List rows. A row's background comes from one of two palettes: the
    /// operator panels (panelRow*: library, media, service) or the dialogs
    /// (dialogRow*). The bar at its left edge is listAccent* in both.
    /// Selection is gray everywhere. A service row that is on the output is
    /// liveRow* instead: that blue tells the operator what is showing, so it
    /// stays distinct from selection.
    readonly property color panelRow: "#1a1a1a"
    readonly property color panelRowHover: "#242424"
    readonly property color panelRowSel: "#252525"
    readonly property color panelRowSelHover: "#363636"
    readonly property color liveRow: "#093e73"
    readonly property color liveRowHover: "#0b4a86"
    readonly property color listAccentHover: "#404040"
    readonly property color listAccentSel: "#808080"
    readonly property color listAccentSelHover: "#9a9a9a"
    /// A one-line list row. Two-line rows, dividers and nested rows size
    /// themselves.
    readonly property int listRowH: 32
    /// Also-play sets. A linked row's chain icon takes its set's color,
    /// cycling through these in rundown order so neighboring sets differ.
    readonly property var linkSetColors: ["#2fb3a0", "#e0a43a", "#a084e8", "#e0707e"]
    function linkSetColor(index) {
        return style.linkSetColors[index % style.linkSetColors.length]
    }

    function panelRowBg(selected, hovered) {
        if (selected)
            return hovered ? style.panelRowSelHover : style.panelRowSel
        return hovered ? style.panelRowHover : style.panelRow
    }
    function listAccent(selected, hovered) {
        if (selected)
            return hovered ? style.listAccentSelHover : style.listAccentSel
        return hovered ? style.listAccentHover : "transparent"
    }
    readonly property color dialogControlBorder: style.controlEdge
    readonly property color dialogControlBorderHover: style.controlEdgeHover
    readonly property color dialogFooterRule: Qt.rgba(1, 1, 1, 0.09)
    /// Title ink on dialog cards - a step brighter than body text.
    readonly property color dialogTitle: "#ffffff"

    /// The window's dim (ModalScrim's tint), which only the first modal open
    /// paints: one stacked over it never darkens the window again.
    /// A small modal dialog (AppDialog / AppPromptDialog: confirms, prompts,
    /// pick-one sheets) opened straight over the operator window dims it
    /// lightly; a full settings sheet (Output / Song / App / Services) deeply.
    readonly property color modalScrim: "#88000000"
    readonly property color sheetScrim: "#d90d0d0d"
    readonly property int modalPad: 14
    readonly property int modalGap: 10
    readonly property int modalRadius: radiusLarge
    readonly property int modalWidth: 420
    /// Overlay stacking, bottom to top. A settings sheet (Output, Song, App,
    /// Services) sits under the freeform editor, which can open over one; a
    /// theme picker opens from either; a modal confirms over all of them.
    readonly property int sheetZ: 1000
    // (The Output sheet is sheetZ + 1: it opens over Settings from its Outputs
    // page; so is the Song sheet Settings' Fonts page opens, and Service
    // Settings, which opens over the Services window.)
    readonly property int editorZ: 1100
    readonly property int pickerZ: 1200
    readonly property int modalZ: 1400

    // ---------------------------------------------------------------- roles --
    // Every colour the UI draws with has a role below or above. A literal colour
    // in a view is a document's own (a default font or background colour) or
    // intrinsic to what it draws (the colour picker's spectrum).

    /// Shared ink greys. Roles that should look alike name one step, so they
    /// cannot drift a few levels apart again. (The surface greys step by one
    /// or two levels on purpose: that is how panels, dialogs and bars stack.)
    readonly property color inkGrey80: "#cccccc"
    readonly property color inkGrey60: "#9a9a9a"
    readonly property color inkGrey54: "#8a8a8a"
    readonly property color inkGrey47: "#777777"

    /// Ink: text and icons, strongest first.
    /// Selected, active and hovered text and icons.
    readonly property color textStrong: "#ffffff"
    /// Headings in dialogs and panels; menu items.
    readonly property color textBright: "#dddddd"
    /// Idle text or an icon under the pointer.
    readonly property color textHover: "#d0d0d0"
    /// Field labels in settings sheets.
    readonly property color textSoft: "#a8a8a8"
    /// Row and menu icons at rest.
    readonly property color iconIdle: style.inkGrey60
    /// Captions, section titles, empty states.
    readonly property color textCaption: style.inkGrey54
    /// Hints under fields.
    readonly property color textHint: style.inkGrey47
    /// Placeholders and disabled text.
    readonly property color textFaint: "#666666"
    /// Small glyphs at rest: the ... button, shape previews.
    readonly property color glyphInk: "#b4b4b4"
    /// Text typed into the library search.
    readonly property color searchInk: "#e0e0e0"
    /// Menu option labels in the controller.
    readonly property color menuInk: style.inkGrey80
    /// Drop-down chevrons on destination pickers.
    readonly property color chevronInk: "#aaaaaa"
    /// Long explanatory notes in settings.
    readonly property color noteInk: style.inkGrey47
    /// Trademark and licence notes.
    readonly property color legalInk: "#6a6a6a"
    /// Neutral button text under the pointer.
    readonly property color btnInkHover: "#dcdcdc"
    /// The "up one folder" glyph.
    readonly property color parentGlyph: "#a0a0a0"
    /// Stack tab label at rest.
    readonly property color stackTabInk: style.inkGrey60
    /// Stack icons at rest; video time labels.
    readonly property color iconSoft: style.inkGrey80
    /// Addresses the user can open (remote URLs).
    readonly property color linkInk: style.onGlyph
    /// A count on a selected row.
    readonly property color countInkSel: "#c8d8ef"
    /// A caption while something can be dropped on it.
    readonly property color dropInk: "#7ab8e8"

    /// Status.
    /// Warnings and notices.
    readonly property color textWarning: "#c9a227"
    /// Folder glyphs.
    readonly property color folderInk: "#c9a227"
    /// Errors and destructive options.
    readonly property color textDanger: "#c07070"
    /// Something working as it should.
    readonly property color textOk: "#7fa86a"
    /// A message waits to be read: the dot on the bar's Collaboration button.
    readonly property color unreadDot: "#f0a030"
    /// Marks a rundown item with its own looks.
    readonly property color overrideDot: "#f0a030"
    /// The remote is running.
    readonly property color runningDot: "#3f9d5a"
    /// A live source's picture is coming; it has stopped (or never came).
    readonly property color signalOk: "#3f9d5a"
    readonly property color signalLost: "#e8912d"

    /// Surfaces.
    /// Pop-up menus and suggestion lists.
    readonly property color menuBg: "#1e1e1e"
    /// Pop-up menu outline.
    readonly property color menuEdge: "#3a3a3a"
    /// Menu option under the pointer.
    readonly property color menuHover: "#2a2a2a"
    /// The chosen row in a stack menu: the live service row's blue.
    readonly property color menuChosen: style.liveRow
    readonly property color menuChosenHover: style.liveRowHover
    /// The current option in a suggestion list.
    readonly property color menuCurrent: "#252525"
    /// Hover on plain list rows (pickers, options).
    readonly property color rowHoverQuiet: "#242424"
    /// Small chips and icon tiles in dialogs.
    readonly property color chipBg: "#222222"
    /// Bars: window title bars and the operator window's bottom bar.
    readonly property color headerBg: "#1b1b1b"
    /// Behind a thumbnail in the Controller's slide gallery.
    readonly property color subheaderBg: "#1a1a1a"
    /// Behind thumbnails and in small fields.
    readonly property color thumbWell: "#141414"
    /// Behind theme previews.
    readonly property color thumbWellDark: "#0d0d0d"
    /// Behind the "inherit" theme card.
    readonly property color inheritWell: "#161616"
    /// A divider row in the rundown.
    readonly property color dividerRow: "#181818"
    /// A dragged row's ghost.
    readonly property color dragGhostBg: "#e0252525"
    /// Drop shadow under menus and cards.
    readonly property color shadow: "#000000"
    /// Behind a colour swatch.
    readonly property color swatchWell: "#3a3a3a"
    /// Section rules and divider lines.
    readonly property color rule: "#3a3a3a"

    /// Edges.
    /// Outline of an idle card or toggle track.
    readonly property color edgeQuiet: "#333333"
    /// Outline of idle chips and switches.
    readonly property color edgeIdle: "#444444"
    /// Field outline under the pointer.
    readonly property color edgeHover: "#505050"
    /// Outline of tiles and chips that stand out; its hover.
    readonly property color edgeStrong: "#555555"
    /// Outline of a selected gallery card.
    readonly property color edgeSelected: "#9a9a9a"
    /// Outline of a gallery card under the pointer.
    readonly property color edgeHoverAccent: "#5a7d9e"
    /// The bar marking a selected row (as listAccentSel).
    readonly property color selectedBar: "#808080"
    /// A row under the pointer; a shown line's rule.
    readonly property color rowHoverBar: "#404040"

    /// Controls.
    /// The 1 px highlight along a raised control's top.
    readonly property color sheen: "#ffffff"
    /// Resize handles on the canvas.
    readonly property color handleFill: "#ffffff"
    readonly property color handleEdge: "#000000"
    /// Switch knobs.
    readonly property color knob: "#ffffff"
    /// A switch's track when off.
    readonly property color toggleOff: "#333333"
    /// A switch's outline when off.
    readonly property color toggleOffEdge: "#4a4a4a"
    /// Video seek knob.
    readonly property color sliderKnob: "#d8d8d8"
    /// Video seek knob under the pointer.
    readonly property color sliderKnobHover: "#f0f0f0"
    /// Video seek knob while dragged.
    readonly property color sliderKnobPressed: "#ffffff"
    /// Video seek knob outline; a checked switch's outline.
    readonly property color sliderKnobEdge: "#002a58"
    /// Video seek track.
    readonly property color sliderTrack: "#2a2a2a"
    /// Video seek track outline.
    readonly property color sliderTrackEdge: "#1a1a1a"
    /// Ribbon stepper button under the pointer.
    readonly property color stepperBgHover: "#2e2e2e"
    /// Colour picker handles, inner ring.
    readonly property color pickerRing: "#ffffff"
    /// Colour picker handles, outer ring.
    readonly property color pickerRingOuter: "#000000"
    /// Transparency checker, light squares.
    readonly property color checkerLight: "#6a6a6a"
    /// Transparency checker, dark squares.
    readonly property color checkerDark: "#3d3d3d"

    /// Canvas and drag-and-drop.
    /// Canvas interaction: marquee outline, snap guides, the box being typed
    /// into, the outline of a shape being drawn.
    readonly property color canvasGuide: "#40c4ff"
    /// A slide on the canvas where nothing is behind its objects, black as the
    /// outputs show it; also behind a video object's picture.
    readonly property color canvasSlideBlack: "#000000"
    /// The mark on a video object on the canvas.
    readonly property color canvasVideoMark: "#ffffff"
    /// Fill of the canvas marquee.
    readonly property color canvasMarqueeFill: Qt.rgba(0.25, 0.77, 1, 0.12)
    /// Rubber-band selection fill in lists.
    readonly property color marqueeFill: Qt.rgba(0.45, 0.62, 0.88, 0.16)
    /// Rubber-band selection outline.
    readonly property color marqueeEdge: "#7eb0e8"
    /// Where a dragged row would land.
    readonly property color dropIndicator: style.focusEdge
    /// Outline under text drawn on thumbnails.
    readonly property color outlineShade: "#80000000"
    /// Over a selected video area in the theme designer.
    readonly property color placeholderVideo: "#22aa6eff"
    /// Over a selected image area, or a picture not chosen yet, in the theme
    /// designer.
    readonly property color placeholderImage: "#2200c8c8"
    /// Over a selected web area in the theme designer.
    readonly property color placeholderWeb: "#22e0a030"
    /// Freeform slide sorter: dragged slide and drop slot.
    readonly property color sorterDragBg: "#1a2838"
    /// Freeform slide sorter: drop slot outline.
    readonly property color sorterDragEdge: "#2a5080"
    /// Freeform slide sorter: dragged slide label.
    readonly property color sorterDragInk: "#d4e4f4"
    /// Behind a slide thumbnail in the sorter.
    readonly property color slideWell: "#0a0a0a"
    /// Around the Freeform stage.
    readonly property color stageWell: "#181818"

    /// Faint fills and tints.
    /// A tab under the pointer; a field row in the Freeform fields list.
    readonly property color tabHoverBg: Qt.rgba(1, 1, 1, 0.03)
    /// A bare button (stack tab, the ... button) under the pointer.
    readonly property color ghostHover: Qt.rgba(1, 1, 1, 0.035)
    /// Rule between option groups in a ribbon menu.
    readonly property color menuRule: Qt.rgba(1, 1, 1, 0.08)
    /// Accent tints: a drop target, the chosen copy source.
    readonly property color dropTint: Qt.rgba(0, 71 / 255, 145 / 255, 0.12)
    readonly property color choiceActiveBg: Qt.rgba(0, 71 / 255, 145 / 255, 0.25)
    /// Outline of the slide on the output, in the controller's gallery.
    readonly property color liveThumbEdge: Qt.rgba(0, 71 / 255, 145 / 255, 0.55)
    /// Label strip over a gallery thumbnail.
    readonly property color thumbLabelBg: Qt.rgba(0, 0, 0, 0.6)
    /// Dims a dialog behind an inline editor (arrangement name).
    readonly property color inlineScrim: Qt.rgba(13 / 255, 13 / 255, 13 / 255, 0.72)

    /// Controller line rows (and the cue list, which follows them).
    /// The line on the output. A touch darker than accent.
    readonly property color lineLive: "#003976"
    readonly property color lineLiveHover: "#0c4e90"
    /// The next line to go out.
    readonly property color lineNext: "#1a2a1a"
    readonly property color lineNextHover: "#1f3220"
    /// Lines on the output's current slide.
    readonly property color lineShown: "#242424"
    readonly property color lineShownHover: "#2a2a2a"
    /// Every other line.
    readonly property color lineIdle: "#141414"
    readonly property color lineIdleHover: "#1e1e1e"
    /// Ink of next and shown lines.
    readonly property color lineInkLit: "#e8e8e8"
    readonly property color lineInkLitHover: "#f2f2f2"
    /// Ink of other lines.
    readonly property color lineInk: style.inkGrey54
    readonly property color lineInkHover: "#a0a0a0"
    /// The rule after the live line's label, and after the next line's.
    readonly property color lineRuleLive: Qt.rgba(1, 1, 1, 0.18)
    readonly property color lineRuleNext: Qt.rgba(14 / 255, 138 / 255, 14 / 255, 0.45)
    /// The edge of the next slide's or image's thumb: that rule's green, whole.
    readonly property color thumbNext: "#0e8a0e"
    /// The rule after an idle line's label.
    readonly property color lineRuleIdle: "#2a2a2a"
    /// A selected cue row.
    readonly property color cueRowSel: "#2a2a2a"
    readonly property color cueRowSelHover: "#333333"
    /// A cue's indicator chip.
    readonly property color cueTag: "#1c3a5a"
    readonly property color cueTagInk: "#c5daf0"
    /// Its chip on the live cue.
    readonly property color cueTagLive: "#e8f0fa"
    readonly property color cueTagLiveInk: "#003976"
    /// A stack's "Auto" tag.
    readonly property color statusTagBg: "#0e2c4d"
    readonly property color statusTagInk: "#8dbdf0"
    /// The "Auto" tag on a chosen (blue) menu row.
    readonly property color statusTagChosenBg: Qt.rgba(0, 0, 0, 0.28)
    readonly property color statusTagChosenInk: "#cfe4fa"

    /// Output cards: a darker well and a clearer edge than the rail panel.
    /// Their blank / freeze toggles light up in btnBlankOnBot and
    /// btnFreezeOnBot.
    readonly property color cardBg: "#0a0a0a"
    readonly property color cardEdge: "#2a2a2a"
    readonly property color cardTopbar: "#181818"
    readonly property color cardHairline: "#2a2a2a"
    /// Output name on its card; a dragged card's label.
    readonly property color cardLabel: "#b9b9b9"
    readonly property color previewWell: "#000000"
    /// An output that is open.
    readonly property color liveGreen: "#1f7d3a"
    /// Outline of an open output's preview.
    readonly property color openOutline: "#3aa0d0"

    /// The Audio Mixer's meters: lit as OBS lights them (green, then yellow
    /// from -20 dB and red from -9 dB), a muted strip's bars, the unlit track
    /// and the peak line's scale marks.
    readonly property color meterGreen: "#3da35a"
    readonly property color meterYellow: "#c9a63a"
    readonly property color meterRed: "#c8473a"
    readonly property color meterMuted: "#4a4a4a"
    readonly property color meterTrack: "#1d1d1d"
    readonly property color meterTick: style.textFaint
    /// Its faders: the groove, the part of it under the handle (the level
    /// let through), and the handle.
    readonly property color faderTrack: "#262626"
    readonly property color faderFill: style.onEdge
    readonly property color faderHandle: "#bdbdbd"
    readonly property color faderHandleHover: "#dddddd"
    readonly property color faderHandleEdge: "#0a0a0a"

    /// Song editor chips: title (gold), verse (blue), theme (purple).
    readonly property color chipTitleBg: "#3a2e14"
    readonly property color chipTitleEdge: "#6a5220"
    readonly property color chipTitleInk: "#d4b060"
    readonly property color chipTitleInkFaint: "#7a6840"
    readonly property color chipVerseBg: "#1a2f45"
    readonly property color chipVerseEdge: "#2a4a6a"
    readonly property color chipVerseInk: "#8ab4d8"
    readonly property color chipVerseInkFaint: "#5a7088"
    readonly property color chipThemeBg: "#2a1f3a"
    readonly property color chipThemeEdge: "#5a3a70"
    readonly property color chipThemeInk: "#c4a0e0"
    readonly property color chipThemeInkFaint: "#7a5a90"

    // ------------------------------------------------------------- unified --
    // The shared look: one dark
    // well, one field, one control edge, one focus blue and one "on" tint.

    /// Control heights. Fields and the buttons beside them; window footer
    /// buttons are btnStandardH, bar buttons btnCompactH, the transport
    /// btnCommandH.
    readonly property int controlH: 26
    /// The bar over a song editor well (Verses, the verse editor).
    readonly property int wellBarH: 40
    /// A row in a grouped list (settings rows, stacks, outputs).
    readonly property int groupRowH: 40
    /// A row in the slide editor's sidebar, whose controls are compact.
    readonly property int sidebarRowH: 32
    /// How far a switched-off row's name and tile fade (a Bible that is off).
    readonly property real offOpacity: 0.45
    /// A pick-list row with a second line; one line is listRowH.
    readonly property int listRowTallH: 44
    /// Field widths, so right edges line up down a form.
    readonly property int fieldShortW: 90
    readonly property int fieldMediumW: 180
    readonly property int fieldLongW: 260
    /// The drag tile at the start of a reorderable row.
    readonly property int tileSize: 28
    /// The Order column of a table in a well (Settings' Outputs, Stacks and
    /// theme priority), so each row's place sits in the same spot on each.
    readonly property int orderColumnW: 46

    /// The dark well behind lists: the Controller, song lyrics, libraries.
    readonly property color wellBg: "#101010"
    /// The header strip over a well.
    readonly property color wellHead: "#181818"
    /// Every well's outline.
    readonly property color wellEdge: "#2a2a2a"
    /// The app's mark in an empty well: a row's grey, barely off the well.
    readonly property color wellMark: style.panelRow
    /// A grouped list in a window; its rows under the pointer and selected.
    readonly property color groupBg: "#1a1a1a"
    readonly property color groupHover: "#1f1f1f"
    readonly property color groupSel: "#242424"
    /// The body of a row opened in place.
    readonly property color groupOpenBg: "#171717"
    /// A grouped list's outline, and the hairline between its rows.
    readonly property color groupEdge: "#2a2a2a"
    readonly property color groupRule: style.border
    /// Every field's face, everywhere; focused.
    readonly property color fieldFace: style.dialogField
    readonly property color fieldFaceFocus: "#161616"
    /// A field's or toggle button's outline; under the pointer.
    readonly property color controlEdge: "#333333"
    readonly property color controlEdgeHover: "#4a4a4a"
    /// Focus, drag and drop, and layout editing: one blue.
    readonly property color focusEdge: "#3a8ad8"
    /// A key named in a hint (Shift, Esc): its face and edge.
    readonly property color keyCap: "#2c2c2c"
    readonly property color keyCapEdge: "#4a4a4a"
    /// Placeholder text in a field.
    readonly property color fieldPlaceholder: style.textCaption
    /// Something switched on (B / I / U, a filter, Edit Mode, author credits):
    /// a blue tint, its edge, its text and its glyph.
    readonly property color onTint: Qt.rgba(0, 71 / 255, 145 / 255, 0.28)
    readonly property color onEdge: "#2a5a8a"
    readonly property color onInk: "#dbe9f8"
    readonly property color onGlyph: "#8dbdf0"
    /// A drag tile (a Library tab's glyph, a build step's number); under the pointer.
    readonly property color tileBg: "#232323"
    readonly property color tileEdge: "#2e2e2e"
    readonly property color tileHoverBg: "#2a2a2a"
    readonly property color tileHoverEdge: style.controlEdgeHover
    /// A tag someone typed (service tags, topics, theme tags).
    readonly property color tagBg: style.chipBg
    readonly property color tagEdge: style.controlEdge
    readonly property color tagInk: style.textSoft
    /// A badge the app shows (TODAY, IN USE, HIGHEST, OFF): grey, or blue.
    readonly property color badgeBg: "#262626"
    readonly property color badgeInk: style.textSoft
    /// A badge over a picture that can be clicked to take its place (a theme
    /// card's Song / Bible): dark, see-through, outlined, legible on any image.
    readonly property color badgeGhostBg: style.thumbLabelBg
    readonly property color badgeGhostEdge: Qt.rgba(1, 1, 1, 0.35)
    readonly property color badgeGhostInk: "#c8c8c8"
    readonly property color badgeBlueBg: style.statusTagBg
    readonly property color badgeBlueInk: style.statusTagInk
    /// Window chrome: the title bar and footer bar, the left rail, the
    /// rail's current page, the window's outline.
    readonly property color windowBar: style.headerBg
    readonly property color windowRail: "#181818"
    readonly property color railItemOn: "#252525"
    readonly property color windowEdge: style.modalEdge
    /// The title bar's and footer bar's height; the rail's width.
    readonly property int windowBarH: 44
    readonly property int windowRailW: 184
    /// The room round a window's page: at its sides, above and below.
    /// ScrollPage runs into it, so its scrollbar sits at the window's edge.
    readonly property int windowPagePadX: 28
    readonly property int windowPagePadTop: 18
    readonly property int windowPagePadBottom: 20

    /// Operator list and menu scrollbar thickness.
    readonly property int scrollSize: 10
    /// Gap between content and the scrollbar track when the bar is shown.
    readonly property int scrollGap: 6
    /// Extra flickable pad when a sticky gutter would otherwise leave a
    /// zero-range handle (16:9 cards that shrink to fit after the bar appears).
    readonly property int scrollMinOverflow: 32
    readonly property color scrollTrack: "#1a1a1a"
    readonly property color scrollThumb: "#3a3a3a"
    readonly property color scrollThumbHover: "#505050"

    /// A computer's picture (``Avatar``): a glyph on a tint of its colour, or
    /// its initials. The glyphs and colours the Sync page offers; the
    /// colours are the also-play sets'.
    readonly property var pictureGlyphs: ["song", "headphones", "mic", "church", "laptop"]
    readonly property var pictureColours: ["#2fb3a0", "#5a9ee0", "#a084e8", "#e0707e", "#e0a43a", style.inkGrey54]
    readonly property real pictureTint: 0.18
    readonly property color pictureInitialsBg: "#2a2a2a"
    readonly property color pictureInitialsInk: style.textSoft
    /// The Collaboration window: a message's writer and time, and a note of
    /// changes (a row as the wells' rows are).
    readonly property color logName: style.textBright
    readonly property color logTime: style.textHint
    readonly property color logNoteInk: style.inkGrey60
    readonly property color logNoteRow: style.panelRow
    readonly property color logNoteRowHover: style.panelRowHover

    // The main page (Phase 8): flat panels docked round the Controller, and
    // the bar along the bottom.
    /// The title bar of each docked panel (DockTitleBar), its rule included.
    readonly property int dockHeaderH: 36
    readonly property color dockHeaderBg: style.headerBg
    /// A panel's title bar under the pointer: pressing it collapses the panel
    /// (and in Edit Mode, dragging it moves the panel).
    readonly property color dockHeaderHover: "#232323"
    /// Edit Mode's outline round each panel.
    readonly property color dockEditEdge: Qt.rgba(58 / 255, 138 / 255, 216 / 255, 0.55)
    /// The label that follows a moved panel.
    readonly property color dockGhostBg: "#202020"
    /// The Library's tab strip: part of its heading, a step under the title bar.
    readonly property color libraryTabsBg: "#181818"
    readonly property int appBarH: 28
    readonly property color appBarBg: style.headerBg
    /// The dot on the bar's Live while it is on: a tally red.
    readonly property color liveDot: "#e0352b"

    /// Reserved width beside an attached vertical AppScrollBar.
    /// Uses the bar's imperative reservedWidth so overflow does not bind
    /// back into Flickable contentWidth/delegate width (startup hang).
    function scrollGutter(flickable) {
        if (!flickable)
            return 0
        const bar = flickable.ScrollBar.vertical
        if (!bar)
            return 0
        return bar.reservedWidth || 0
    }

    /// Reserved height above an attached horizontal AppScrollBar.
    function scrollGutterH(flickable) {
        if (!flickable)
            return 0
        const bar = flickable.ScrollBar.horizontal
        if (!bar)
            return 0
        return bar.reservedHeight || 0
    }
}
