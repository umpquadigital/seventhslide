pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import SeventhSlide

// The stage: the slide in its output's shape, in a well, with its objects
// drawn as the output draws them and picked, moved and typed into. A theme
// shows its draft raster (core paints the design with sample content) with
// the boxes over it; the box being typed into is drawn here instead.
Rectangle {
    id: stage
    required property SlideEditState dialog
    readonly property SessionFacade session: stage.dialog.session
    readonly property SlideEditSession edit: stage.dialog.edit
    readonly property int handleHit: 10
    readonly property int snapThreshold: 10
    readonly property int inset: 24

    /// Stage pixels per canvas pixel: the slide fitted into the well, or the
    /// zoom chosen.
    readonly property real fitScale: {
        const availW = Math.max(40, stage.width - 2 * stage.inset)
        const availH = Math.max(40, stage.height - 2 * stage.inset)
        return Math.min(availW / Math.max(1, stage.dialog.canvasW), availH / Math.max(1, stage.dialog.canvasH))
    }
    /// The height that shows the slide at ``width`` with nothing to spare.
    function fittedHeight(width) {
        return Math.max(40, width - 2 * stage.inset) * stage.dialog.canvasH / Math.max(1, stage.dialog.canvasW)
               + 2 * stage.inset
    }
    readonly property var slideBg: stage.edit && stage.dialog.isFreeform ? stage.edit.slides.background : ({})
    readonly property real stageScale: stage.dialog.zoom > 0 ? stage.dialog.zoom : stage.fitScale
    onStageScaleChanged: stage.dialog.stageScale = stage.stageScale
    Component.onCompleted: stage.dialog.stageScale = stage.stageScale

    function objRect(obj) {
        const previews = canvasStage.previewRects
        if (previews && previews.length && obj) {
            for (let i = 0; i < previews.length; ++i) {
                if (previews[i] && previews[i].id === obj.id)
                    return previews[i]
            }
        }
        const preview = canvasStage.preview
        if (preview && obj && preview.id === obj.id)
            return preview
        return { x: obj.x, y: obj.y, w: obj.width, h: obj.height, rotation: obj.rotation }
    }
    /// Zoom by ``factor`` about ``scenePoint`` (in the window): the part of
    /// the slide under it stays under it.
    function zoomAbout(factor, scenePoint) {
        const at = flick.mapFromItem(null, scenePoint.x, scenePoint.y)
        const before = stage.stageScale
        const after = stage.dialog.clampZoom(before * factor)
        if (Math.abs(after - before) < 1e-4)
            return
        // The slide point under ``at``, in canvas pixels.
        const cx = (at.x + flick.contentX - slide.x) / before
        const cy = (at.y + flick.contentY - slide.y) / before
        stage.dialog.zoom = after
        // The slide has its new size and place now (bindings follow at once).
        flick.contentX = Math.max(0, Math.min(flick.contentWidth - flick.width, cx * after + slide.x - at.x))
        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height, cy * after + slide.y - at.y))
    }
    /// Settle a gesture in progress.
    function finishGesture() { canvasStage.finish() }
    /// The text edit, for the Fields window to type into.
    function insertAtCaret(text) {
        if (!stage.dialog.textEditing)
            return
        stageEdit.insert(stageEdit.cursorPosition, text)
    }
    function replaceSelection(text) {
        if (!stage.dialog.textEditing)
            return
        const a = Math.min(stageEdit.selectionStart, stageEdit.selectionEnd)
        const b = Math.max(stageEdit.selectionStart, stageEdit.selectionEnd)
        if (b > a)
            stageEdit.remove(a, b)
        stageEdit.insert(a, text)
    }
    function selectedText() {
        return stage.dialog.textEditing ? stageEdit.selectedText : ""
    }

    color: Style.stageWell
    radius: Style.radiusLarge
    border.color: Style.wellEdge
    clip: true

    // Ctrl and the wheel zoom about the pointer; the wheel alone scrolls.
    WheelHandler {
        id: zoomWheel
        objectName: "stageZoomWheel"
        parent: flick
        target: null
        acceptedModifiers: Qt.ControlModifier
        onWheel: function (event) {
            const notches = event.angleDelta.y / 120
            if (notches !== 0)
                stage.zoomAbout(Math.pow(1.1, notches), zoomWheel.point.scenePosition)
        }
    }

    // Pinching zooms about the fingers: a trackpad's pinch (macOS sends it
    // as a gesture of its own) or two on a touch screen.
    PinchHandler {
        id: zoomPinch
        objectName: "stageZoomPinch"
        parent: flick
        target: null
        onScaleChanged: function (delta) {
            if (delta > 0)
                stage.zoomAbout(delta, zoomPinch.centroid.scenePosition)
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: Math.max(width, slide.width + 2 * stage.inset)
        contentHeight: Math.max(height, slide.height + 2 * stage.inset)
        clip: true
        interactive: stage.dialog.zoom > 0
        boundsBehavior: Flickable.StopAtBounds

        Rectangle {
            id: slide
            objectName: "stageSlide"
            x: Math.round((flick.contentWidth - width) / 2)
            y: Math.round((flick.contentHeight - height) / 2)
            width: Math.round(stage.dialog.canvasW * stage.stageScale)
            height: Math.round(stage.dialog.canvasH * stage.stageScale)
            color: stage.dialog.isTheme ? Style.thumbWellDark : Style.canvasSlideBlack
            clip: true

            // ---- what is behind the objects
            TransparencyGrid {
                anchors.fill: parent
                visible: stage.dialog.isTheme
                cellPx: 24
            }
            Image {
                id: draftRaster
                anchors.fill: parent
                visible: stage.dialog.isTheme
                fillMode: Image.Stretch
                asynchronous: false
                cache: false
                // Painted at the size it shows, words and all: a full-size
                // raster squeezed by the GPU shimmers and goes jagged.
                sourceSize: Qt.size(slide.width * Screen.devicePixelRatio, slide.height * Screen.devicePixelRatio)
                smooth: true
                source: stage.dialog.isTheme ? "image://themepreview/draft?r=" + stage.dialog.draftRevision : ""
            }
            Rectangle {
                anchors.fill: parent
                visible: stage.dialog.isFreeform && stage.slideBg.kind === "color"
                color: visible ? String(stage.slideBg.color) : "transparent"
            }
            // The slide's picture, read off the window's thread at the size it
            // shows. While the next one loads, the last stays if it is the
            // same picture (the stage resized), and none shows if it is
            // another, so a slide never shows the one before it.
            Image {
                id: slidePicture
                objectName: "stageBackgroundImage"
                readonly property string name: stage.dialog.isFreeform && stage.slideBg.kind === "image"
                                               ? String(stage.slideBg.image || "") : ""
                /// The picture shown when the image last loaded.
                property string shownName: ""
                function commit() {
                    if (slidePicture.status === Image.Ready)
                        slidePicture.shownName = slidePicture.name
                }
                anchors.fill: parent
                visible: name.length > 0 && (status === Image.Ready || shownName === name)
                source: name.length > 0 && width > 0
                        ? stage.session.media.mediaFileUrl(MediaKind.Image, name, stage.session.media.filesRevision)
                        : ""
                sourceSize: Qt.size(Math.max(1, Math.round(width * Screen.devicePixelRatio)),
                                    Math.max(1, Math.round(height * Screen.devicePixelRatio)))
                asynchronous: true
                retainWhileLoading: true
                fillMode: Image.PreserveAspectCrop
                mipmap: true
                onStatusChanged: commit()
                // A picture already in the cache is ready at once, with no
                // change of status to say so.
                onSourceChanged: Qt.callLater(slidePicture.commit)
            }
            // A video background shows its still on the stage.
            Image {
                objectName: "stageBackgroundVideo"
                anchors.fill: parent
                visible: stage.dialog.isFreeform && stage.slideBg.kind === "video" && String(stage.slideBg.video || "").length > 0
                source: visible ? stage.session.media.mediaThumbUrl(MediaKind.Video, stage.slideBg.video,
                                                                    stage.session.media.filesRevision) : ""
                fillMode: Image.PreserveAspectCrop
            }
            PicturePlaceholder {
                anchors.fill: parent
                session: stage.session
                wanted: stage.dialog.isFreeform && stage.slideBg.kind === "image"
                filename: String(stage.slideBg.image || "")
            }

            // ---- the objects: rows kept by id, so an edit redraws only what
            // it changed; each loads only the parts its kind draws.
            Repeater {
                model: stage.edit ? stage.edit.objects.canvasModel : null
                delegate: Item {
                    id: box
                    required property var obj
                    readonly property var rect: stage.objRect(box.obj)
                    readonly property real sc: stage.stageScale
                    readonly property bool isSelected: !!stage.edit
                        && stage.edit.objects.selectedIds.indexOf(box.obj.id) >= 0
                    readonly property bool isEditing: stage.dialog.textEditing
                                                      && stage.dialog.textEditId === box.obj.id
                    readonly property real strokePx: Number(box.obj.stroke_width) * sc
                    /// How far the box's effects reach past it, in stage px.
                    readonly property real effectsPx: Number(box.obj.effects_reach || 0) * sc
                    readonly property bool textEffects: !!box.obj.text_shadow_on
                        || (Number(box.obj.text_outline_width) > 0
                            && String(box.obj.text_outline_color || "").length > 0)
                    readonly property bool themeMode: stage.dialog.isTheme
                    // A theme's raster paints every element, each at its own
                    // place in the stack, the sample lyrics and the stand-ins
                    // for media too. QML draws only chrome over it, and the
                    // box being typed into.
                    readonly property bool textLike: box.obj.type === "text" || box.obj.type === "indicator"
                                                     || box.obj.type === "clock"
                    // A linked box, or one whose words have effects, is
                    // drawn as the output draws it: rich text cannot put
                    // chords over words, or a shadow under them.
                    readonly property bool drawsAsOutput: !box.themeMode && box.obj.type === "text"
                                                          && (!!box.obj.cue_linked || box.textEffects
                                                              || !!box.obj.paragraph_set)
                    x: rect.x * sc
                    y: rect.y * sc
                    width: Math.max(4, rect.w * sc)
                    height: Math.max(4, rect.h * sc)
                    rotation: Number(rect.rotation) || 0
                    z: Number(box.obj.z)
                    readonly property real objectOpacity: Number(box.obj.opacity)

                    Loader {
                        anchors.fill: parent
                        active: !box.themeMode && box.obj.type === "shape"
                        sourceComponent: Component {
                            Item {
                                FreeformShapeItem {
                                    id: shapeItem
                                    // An open outline's arrowhead reaches past its end by
                                    // up to five of the line's width (render's shape_reach).
                                    readonly property real pad: Math.ceil(Math.max(0, box.strokePx)
                                                                          * ((box.obj.shape_kind === "drawn" && !box.obj.shape_closed) ? 5 : 0.5)
                                                                          + box.effectsPx) + 1
                                    anchors.fill: parent
                                    anchors.margins: -pad
                                    opacity: box.objectOpacity
                                    kind: String(box.obj.shape_kind)
                                    session: stage.session
                                    fill: String(box.obj.fill)
                                    fillKind: String(box.obj.fill_kind)
                                    fillPictureFile: String(box.obj.fill_picture_file || "")
                                    fillPictureFit: String(box.obj.fill_picture_fit)
                                    gradientKind: String(box.obj.gradient_kind)
                                    gradientAngle: Number(box.obj.gradient_angle)
                                    gradientCenterX: Number(box.obj.gradient_center_x)
                                    gradientCenterY: Number(box.obj.gradient_center_y)
                                    gradientFrom: String(box.obj.gradient_from || "")
                                    gradientTo: String(box.obj.gradient_to || "")
                                    stroke: String(box.obj.stroke)
                                    strokeWidth: Number(box.obj.stroke_width)
                                    dash: String(box.obj.line_dash)
                                    effects: box.obj.effects || ({})
                                    scaleFactor: box.sc
                                    corner: Number(box.obj.shape_corner)
                                    points: Number(box.obj.shape_points)
                                    inner: Number(box.obj.shape_inner)
                                    head: Number(box.obj.shape_head)
                                    thickness: Number(box.obj.shape_thickness)
                                    path: box.obj.shape_path || []
                                    closed: !!box.obj.shape_closed
                                    lineStart: String(box.obj.line_start)
                                    lineEnd: String(box.obj.line_end)
                                    padding: pad
                                    transform: Scale {
                                        origin.x: shapeItem.width / 2
                                        origin.y: shapeItem.height / 2
                                        xScale: box.obj.flip_h ? -1 : 1
                                        yScale: box.obj.flip_v ? -1 : 1
                                    }
                                }
                            }
                        }
                    }
                    // A text box's frame: its fill and line behind the words,
                    // as the output paints them. (A theme's raster paints its own.)
                    Loader {
                        anchors.fill: parent
                        active: !box.themeMode && box.textLike
                                && (box.obj.fill_kind !== "none"
                                    || (String(box.obj.stroke || "").length > 0
                                        && Number(box.obj.stroke_width) > 0))
                        sourceComponent: Component {
                            Item {
                                FreeformShapeItem {
                                    id: frameItem
                                    readonly property real pad: Math.ceil(Math.max(0, box.strokePx) * 0.5 + box.effectsPx) + 1
                                    anchors.fill: parent
                                    anchors.margins: -pad
                                    opacity: box.objectOpacity
                                    kind: "rectangle"
                                    session: stage.session
                                    fill: String(box.obj.fill || "")
                                    fillKind: String(box.obj.fill_kind)
                                    fillPictureFile: String(box.obj.fill_picture_file || "")
                                    fillPictureFit: String(box.obj.fill_picture_fit)
                                    gradientKind: String(box.obj.gradient_kind)
                                    gradientAngle: Number(box.obj.gradient_angle)
                                    gradientCenterX: Number(box.obj.gradient_center_x)
                                    gradientCenterY: Number(box.obj.gradient_center_y)
                                    gradientFrom: String(box.obj.gradient_from || "")
                                    gradientTo: String(box.obj.gradient_to || "")
                                    stroke: String(box.obj.stroke || "")
                                    strokeWidth: Number(box.obj.stroke_width)
                                    dash: String(box.obj.line_dash)
                                    effects: box.obj.effects || ({})
                                    scaleFactor: box.sc
                                    padding: pad
                                }
                            }
                        }
                    }
                    // A picture as the output paints it: cropped, fitted,
                    // recoloured, see-through, with its effects round it.
                    Loader {
                        anchors.fill: parent
                        active: !box.themeMode && box.obj.type === "image"
                                && String(box.obj.filename || "").length > 0
                        sourceComponent: Component {
                            Item {
                                FreeformPictureItem {
                                    id: imageItem
                                    readonly property real pad: Math.ceil(box.effectsPx) + 1
                                    anchors.fill: parent
                                    anchors.margins: -pad
                                    opacity: box.objectOpacity
                                    transform: Scale {
                                        origin.x: imageItem.width / 2
                                        origin.y: imageItem.height / 2
                                        xScale: box.obj.flip_h ? -1 : 1
                                        yScale: box.obj.flip_v ? -1 : 1
                                    }
                                    session: stage.session
                                    filename: String(box.obj.filename || "")
                                    fit: String(box.obj.object_fit)
                                    picture: box.obj.picture || ({})
                                    effects: box.obj.effects || ({})
                                    scaleFactor: box.sc
                                    padding: pad
                                }
                                PicturePlaceholder {
                                    anchors.fill: parent
                                    opacity: box.objectOpacity
                                    session: stage.session
                                    wanted: imageItem.visible
                                    filename: imageItem.filename
                                }
                            }
                        }
                    }
                    // A table as the output draws it: even cells, the header
                    // tinted and bold, every other row banded, the borders.
                    Loader {
                        anchors.fill: parent
                        active: !box.themeMode && box.obj.type === "table"
                        sourceComponent: Component {
                            Item {
                                Item {
                                    id: tableItem
                                    objectName: "stageTable"
                                    anchors.fill: parent
                                    visible: !box.themeMode && box.obj.type === "table"
                                    opacity: box.objectOpacity
                                    readonly property int rows: Number(box.obj.table_rows) || 0
                                    readonly property int cols: Number(box.obj.table_columns) || 0
                                    readonly property color border: String(box.obj.table_border_color)
                                    readonly property real line: Math.max(0, Number(box.obj.table_border_width)) * box.sc
                                    Repeater {
                                        model: tableItem.visible ? tableItem.rows * tableItem.cols : 0
                                        delegate: Rectangle {
                                            id: cell
                                            required property int index
                                            readonly property int row: Math.floor(cell.index / tableItem.cols)
                                            readonly property bool header: !!box.obj.table_header && cell.row === 0
                                            readonly property bool banded: !!box.obj.table_banding && !cell.header
                                                && (cell.row - (box.obj.table_header ? 1 : 0)) % 2 === 1
                                            x: (cell.index % tableItem.cols) * tableItem.width / tableItem.cols
                                            y: cell.row * tableItem.height / tableItem.rows
                                            width: tableItem.width / tableItem.cols
                                            height: tableItem.height / tableItem.rows
                                            color: cell.header ? Qt.rgba(tableItem.border.r, tableItem.border.g, tableItem.border.b, 0.3)
                                                 : cell.banded ? Qt.rgba(tableItem.border.r, tableItem.border.g, tableItem.border.b, 0.12)
                                                 : "transparent"
                                            border.color: tableItem.border
                                            // At least a pixel, so it shows at any zoom.
                                            border.width: tableItem.line > 0 ? Math.max(1, tableItem.line / 2) : 0
                                            Text {
                                                // Turns with its box: the app's
                                                // native text would be a
                                                // picture of it, rotated.
                                                renderType: Text.QtRendering
                                                anchors.fill: parent
                                                anchors.leftMargin: Math.max(4, Number(box.obj.font_size) * 0.3) * box.sc
                                                anchors.rightMargin: anchors.leftMargin
                                                verticalAlignment: String(box.obj.valign) === "top" ? Text.AlignTop
                                                                 : String(box.obj.valign) === "bottom" ? Text.AlignBottom
                                                                 : Text.AlignVCenter
                                                horizontalAlignment: String(box.obj.align) === "center" ? Text.AlignHCenter
                                                                   : String(box.obj.align) === "right" ? Text.AlignRight
                                                                   : Text.AlignLeft
                                                wrapMode: Text.WordWrap
                                                clip: true
                                                text: String((box.obj.table_cells || [])[cell.index] || "")
                                                color: String(box.obj.font_color)
                                                font.family: stage.dialog.painted(box.obj.font_family)
                                                font.pixelSize: Math.max(1, Number(box.obj.font_size) * box.sc)
                                                font.bold: cell.header || !!box.obj.bold
                                                font.italic: !!box.obj.italic
                                                font.underline: !!box.obj.underline
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                    // A video: its poster, else the library's picture of it,
                    // on black, marked as a video. It plays on the outputs.
                    Loader {
                        anchors.fill: parent
                        active: !box.themeMode && box.obj.type === "video"
                        sourceComponent: Component {
                            Item {
                                Rectangle {
                                    objectName: "stageVideo"
                                    anchors.fill: parent
                                    visible: !box.themeMode && box.obj.type === "video"
                                    color: Style.canvasSlideBlack
                                    opacity: box.objectOpacity
                                    clip: true
                                    readonly property string fit: String(box.obj.object_fit)
                                    readonly property string poster: String(box.obj.video_poster || "")
                                    Image {
                                        anchors.fill: parent
                                        asynchronous: true
                                        fillMode: parent.fit === "cover" ? Image.PreserveAspectCrop
                                                : parent.fit === "fill" ? Image.Stretch
                                                : Image.PreserveAspectFit
                                        source: !parent.visible || !stage.session ? ""
                                              : parent.poster.length
                                                ? stage.session.media.mediaThumbUrl(MediaKind.Image, parent.poster,
                                                                                    stage.session.media.filesRevision)
                                                : stage.session.media.mediaThumbUrl(MediaKind.Video,
                                                                                    String(box.obj.filename || ""),
                                                                                    stage.session.media.filesRevision)
                                    }
                                    Image {
                                        anchors.centerIn: parent
                                        width: Math.min(32, parent.width / 3)
                                        height: width
                                        source: parent.visible ? Icons.url("video", Style.canvasVideoMark) : ""
                                        opacity: 0.8
                                    }
                                }
                            }
                        }
                    }
                    // A linked box is drawn as the output draws it: rich text
                    // cannot put chords over words.
                    Loader {
                        anchors.fill: parent
                        active: box.drawsAsOutput
                        sourceComponent: Component {
                            Item {
                                OverlayTextItem {
                                    // Room past the box for the words' shadow; the words
                                    // themselves stay clipped to the box.
                                    anchors.fill: parent
                                    anchors.margins: -Math.ceil(box.effectsPx)
                                    padding: Math.ceil(box.effectsPx)
                                    visible: box.drawsAsOutput && !box.isEditing
                                    opacity: box.objectOpacity
                                    effects: box.obj.effects || ({})
                                    paragraph: box.obj.paragraph || ({})
                                    session: stage.session
                                    markup: box.drawsAsOutput ? String(box.obj.text || "") : ""
                                    designScale: box.sc
                                    // The library's name: the output's painter finds
                                    // the font by it, not by the name Qt gave the file.
                                    fontFamily: String(box.obj.font_family || "")
                                    fontSize: Number(box.obj.font_size)
                                    fontColor: String(box.obj.font_color)
                                    bold: !!box.obj.bold
                                    italic: !!box.obj.italic
                                    underline: !!box.obj.underline
                                    textAlign: String(box.obj.align)
                                    verticalAlign: String(box.obj.valign)
                                    lineHeight: stage.dialog.objectLineHeight(box.obj)
                                    lineGap: stage.dialog.objectLineGap(box.obj)
                                    gapsAbove: box.obj.gaps_above || []
                                    letterSpacing: stage.dialog.objectLetterSpacing(box.obj)
                                    showChords: !!box.obj.show_chords
                                    chordSizePct: Number(box.obj.chord_size_pct)
                                    chordGapPct: Number(box.obj.chord_gap_pct)
                                    chordColor: String(box.obj.chord_color || "")
                                }
                            }
                        }
                    }
                    StageBox {
                        anchors.fill: parent
                        selected: box.isSelected
                        editing: box.isEditing
                        resizable: box.isSelected && !box.isEditing && stage.dialog.hasSingleSel
                                   && !box.obj.locked
                        showContent: !box.themeMode && !box.drawsAsOutput && box.textLike
                        contentOpacity: box.objectOpacity
                        outlineWhenUnselected: false
                        contentHtml: !box.themeMode && box.textLike ? stage.dialog.objectRichHtml(box.obj, box.sc) : ""
                        selectedTint: box.themeMode ? stage.dialog.stageStandInTint(box.obj) : "transparent"
                        fontColor: String(box.obj.font_color)
                        fontFamily: stage.dialog.painted(box.obj.font_family)
                        fontPixelSize: Math.max(8, Math.round(Number(box.obj.font_size) * box.sc))
                        fontBold: !!box.obj.bold
                        fontItalic: !!box.obj.italic
                        fontUnderline: !!box.obj.underline
                        lineHeight: stage.dialog.objectLineHeight(box.obj)
                        textAlign: String(box.obj.align)
                        vAlign: String(box.obj.valign)
                        wrapMode: Text.WordWrap
                        contentMargins: 0
                    }
                }
            }

            // ---- the box being typed into
            // Over everything, a theme's raster included, on purpose: the
            // words being typed stay in view whatever is stacked over their
            // box. The raster leaves that box out while it is typed into.
            TextEdit {
                id: stageEdit
                objectName: "stageTextEdit"
                visible: stage.dialog.textEditing
                z: 12
                clip: true
                x: Number(stage.dialog.sel.x) * stage.stageScale
                y: Number(stage.dialog.sel.y) * stage.stageScale
                width: Math.max(20, Number(stage.dialog.sel.width) * stage.stageScale)
                height: Math.max(20, Number(stage.dialog.sel.height) * stage.stageScale)
                textFormat: TextEdit.RichText
                padding: stage.dialog.isTheme ? 4 : 0
                // Room over the first line for its chord labels.
                topPadding: stage.dialog.editChordRoom
                            ? Math.round(stage.edit.objects.chordRoom * stage.stageScale)
                            : padding
                color: String(stage.dialog.sel.font_color)
                font.family: stage.dialog.painted(stage.dialog.sel.font_family)
                font.pixelSize: Math.max(8, Math.round(Number(stage.dialog.sel.font_size) * stage.stageScale))
                wrapMode: TextEdit.WordWrap
                selectByMouse: true
                persistentSelection: true
                horizontalAlignment: {
                    if (stage.dialog.sel.align === "left")
                        return TextEdit.AlignLeft
                    if (stage.dialog.sel.align === "right")
                        return TextEdit.AlignRight
                    return TextEdit.AlignHCenter
                }
                verticalAlignment: {
                    if (stage.dialog.sel.valign === "top")
                        return TextEdit.AlignTop
                    if (stage.dialog.sel.valign === "bottom")
                        return TextEdit.AlignBottom
                    return TextEdit.AlignVCenter
                }
                property int loadedHtmlRev: -1
                property int frozenSelStart: 0
                property int frozenSelEnd: 0

                /// The caret takes the type of the words it sits in. A
                /// TextEdit carries the last text's over new text, and only a
                /// caret that moves lets it go: so it moves away and back, or
                /// in an empty box a character comes and goes.
                property bool refreshingCaret: false
                function freshCaret() {
                    // Not a move the operator made: the selection kept stays.
                    stageEdit.refreshingCaret = true
                    const at = stageEdit.cursorPosition
                    if (stageEdit.length > 0) {
                        const away = at > 0 ? 0 : 1
                        stageEdit.select(away, away)
                        stageEdit.select(at, at)
                    } else {
                        // Not a space: rich text drops a lone one.
                        stageEdit.insert(0, "x")
                        stageEdit.remove(0, 1)
                    }
                    stageEdit.refreshingCaret = false
                }
                function reloadHtmlFromRoot() {
                    if (!stage.dialog.textEditing)
                        return
                    if (stage.dialog.editHtmlRev === stageEdit.loadedHtmlRev)
                        return
                    const keepA = Math.min(stage.dialog.editSelStart, stage.dialog.editSelEnd)
                    const keepB = Math.max(stage.dialog.editSelStart, stage.dialog.editSelEnd)
                    stage.dialog.editHtmlSuppress = true
                    stageEdit.text = stage.dialog.editHtml
                    // The caret placed afresh takes its type from the words
                    // it sits in: over empty text a TextEdit keeps the last
                    // text's, and typing would show in it.
                    stageEdit.freshCaret()
                    stageEdit.loadedHtmlRev = stage.dialog.editHtmlRev
                    stage.dialog.applyCanvasTextLayout(stageEdit)
                    Qt.callLater(function () {
                        stage.dialog.applyCanvasTextLayout(stageEdit)
                        stage.dialog.editHtmlSuppress = false
                        if (keepB > keepA) {
                            stageEdit.select(keepA, keepB)
                            stageEdit.frozenSelStart = keepA
                            stageEdit.frozenSelEnd = keepB
                            stage.dialog.editSelStart = keepA
                            stage.dialog.editSelEnd = keepB
                        }
                    })
                }
                function syncSelectionToRoot() {
                    if (!stage.dialog.textEditing || stageEdit.refreshingCaret)
                        return
                    if (!stageEdit.activeFocus && stageEdit.frozenSelEnd > stageEdit.frozenSelStart) {
                        stage.dialog.editSelStart = stageEdit.frozenSelStart
                        stage.dialog.editSelEnd = stageEdit.frozenSelEnd
                        return
                    }
                    stage.dialog.editSelStart = stageEdit.selectionStart
                    stage.dialog.editSelEnd = stageEdit.selectionEnd
                    if (stageEdit.selectionEnd > stageEdit.selectionStart) {
                        stageEdit.frozenSelStart = stageEdit.selectionStart
                        stageEdit.frozenSelEnd = stageEdit.selectionEnd
                    }
                    stage.dialog.editSelTick = stage.dialog.editSelTick + 1
                }
                onTextChanged: {
                    if (stage.dialog.editHtmlSuppress || !stage.dialog.textEditing || !stage.edit)
                        return
                    const plain = TextEditTools.plainText(stageEdit)
                    if (stage.dialog.sel.cue_linked
                            && !stage.edit.cues.linkedEditAllowed(plain, stage.dialog.editUpcoming)) {
                        stage.dialog.editHtmlSuppress = true
                        stage.dialog.refreshEditHtml()
                        Qt.callLater(function () { stage.dialog.editHtmlSuppress = false })
                        return
                    }
                    stage.dialog.textEditDraft = plain
                    stage.dialog.richEdit.reconcile(plain)
                    stage.dialog.editLines = stage.dialog.richEdit.lines
                }
                onSelectionStartChanged: stageEdit.syncSelectionToRoot()
                onSelectionEndChanged: stageEdit.syncSelectionToRoot()
                onActiveFocusChanged: {
                    if (!stageEdit.activeFocus && stage.dialog.textEditing)
                        stageEdit.syncSelectionToRoot()
                }
                onVisibleChanged: {
                    if (visible) {
                        stageEdit.loadedHtmlRev = -1
                        stageEdit.frozenSelStart = 0
                        stageEdit.frozenSelEnd = 0
                        stageEdit.reloadHtmlFromRoot()
                        Qt.callLater(function () { stageEdit.forceActiveFocus() })
                    }
                }
                Connections {
                    target: stage.dialog
                    function onEditHtmlRevChanged() {
                        if (stage.dialog.textEditing)
                            stageEdit.reloadHtmlFromRoot()
                    }
                }
                // Escape ends the typing and keeps it, as a click elsewhere does.
                Keys.onEscapePressed: stage.dialog.endTextEdit(true)
                Keys.onPressed: function (event) {
                    if (!stage.dialog.sel.cue_linked)
                        return
                    // A linked box keeps its cues' characters: only line
                    // breaks may change.
                    const nav = event.key === Qt.Key_Left || event.key === Qt.Key_Right
                              || event.key === Qt.Key_Up || event.key === Qt.Key_Down
                              || event.key === Qt.Key_Home || event.key === Qt.Key_End
                              || event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab
                    if (nav || event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                        return
                    if (event.modifiers & Qt.ControlModifier) {
                        if (event.key === Qt.Key_C || event.key === Qt.Key_A || event.key === Qt.Key_Insert)
                            return
                        event.accepted = true
                        return
                    }
                    // Meta is macOS's Control key, whose Emacs keys (^K, ^H, ^D)
                    // cut and delete.
                    if (event.modifiers & Qt.MetaModifier) {
                        event.accepted = true
                        return
                    }
                    const draft = String(stage.dialog.textEditDraft || "")
                    const a = Math.min(stageEdit.selectionStart, stageEdit.selectionEnd)
                    const b = Math.max(stageEdit.selectionStart, stageEdit.selectionEnd)
                    if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
                        // Alt (macOS's Option) deletes a word, not a line break.
                        if ((event.modifiers & Qt.AltModifier) && b === a) {
                            event.accepted = true
                            return
                        }
                        if (b > a) {
                            if (/[^\n\r]/.test(draft.slice(a, b)))
                                event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Backspace && a > 0
                                && draft.charAt(a - 1) !== "\n" && draft.charAt(a - 1) !== "\r") {
                            event.accepted = true
                            return
                        }
                        if (event.key === Qt.Key_Delete && a < draft.length
                                && draft.charAt(a) !== "\n" && draft.charAt(a) !== "\r") {
                            event.accepted = true
                            return
                        }
                        return
                    }
                    if (event.text && event.text.length && event.text !== "\n" && event.text !== "\r")
                        event.accepted = true
                }
            }

            // The chords of a linked box being edited, over their words, when
            // the box shows them.
            Item {
                id: chordLabels
                visible: stage.dialog.editChordRoom
                x: stageEdit.x
                y: stageEdit.y
                width: stageEdit.width
                height: stageEdit.height
                z: 13
                clip: true
                readonly property int labelPx: Math.max(8, Math.round(
                    Number(stage.dialog.sel.font_size) * stage.stageScale
                    * Number(stage.dialog.sel.chord_size_pct) / 100))
                readonly property var places: {
                    // Read as values: where a character sits moves with the
                    // text's layout, which positionToRectangle does not say.
                    if (!stage.dialog.editChordRoom || stage.dialog.editHtmlRev < 0
                            || stageEdit.contentHeight < 0 || stageEdit.width < 0
                            || stageEdit.topPadding < 0)
                        return []
                    const raw = stage.dialog.editChordPlaces()
                    const out = []
                    let lineY = NaN
                    let freeAt = 0
                    for (let i = 0; i < raw.length; ++i) {
                        const r = stageEdit.positionToRectangle(raw[i].pos)
                        const label = String(raw[i].chord).replace(/b/g, "♭").replace(/#/g, "♯")
                        if (!(Math.abs(r.y - lineY) < 1.5)) {
                            lineY = r.y
                            freeAt = r.x
                        }
                        const x = Math.max(r.x, freeAt)
                        freeAt = x + chordLabelMetrics.advanceWidth(label) + 4
                        out.push({ x: x, y: r.y, label: label })
                    }
                    return out
                }
                FontMetrics {
                    id: chordLabelMetrics
                    font.family: stage.dialog.painted(stage.dialog.sel.font_family)
                    font.pixelSize: chordLabels.labelPx
                    font.bold: true
                }
                Repeater {
                    model: chordLabels.places
                    delegate: Text {
                        required property var modelData
                        x: modelData.x
                        y: modelData.y - chordLabelMetrics.ascent
                        text: modelData.label
                        color: String(stage.dialog.sel.chord_color || "")
                        font.family: stage.dialog.painted(stage.dialog.sel.font_family)
                        font.pixelSize: chordLabels.labelPx
                        font.bold: true
                    }
                }
            }

            CanvasStage {
                id: canvasStage
                anchors.fill: parent
                z: stage.dialog.textEditing ? 1 : 10
                canvasWidth: stage.dialog.canvasW
                canvasHeight: stage.dialog.canvasH
                scaleFactor: stage.stageScale
                objects: stage.dialog.canvasObjects
                selectedId: stage.dialog.hasSel ? String(stage.dialog.sel.id) : ""
                selectedIds: stage.edit ? stage.edit.objects.selectedIds : []
                editingId: stage.dialog.textEditing ? stage.dialog.textEditId : ""
                snapThreshold: stage.snapThreshold
                handleHit: stage.handleHit
                onInteractionBegan: {
                    // Out of any field, so what it holds lands first and the
                    // keys reach the editor.
                    stage.forceActiveFocus()
                    if (stage.dialog.textEditing)
                        stage.dialog.endTextEdit(true)
                }
                onSelected: function (objectId, additive) { stage.edit.objects.selectObject(objectId, !!additive) }
                onMarqueeSelected: function (ids, additive) { stage.edit.objects.selectIds(ids, !!additive) }
                onCleared: stage.edit.objects.clearSelection()
                onCommittedSet: function (geometries) {
                    stage.edit.objects.beginGesture()
                    stage.edit.objects.setGeometries(geometries)
                    stage.edit.objects.endGesture()
                }
                onDragFinished: function (moved) { stage.dialog.stageDragging = false }
                onDraggingChanged: stage.dialog.stageDragging = canvasStage.dragging
                onDoubleClicked: function (objectId) {
                    const obj = stage.dialog.objectById(objectId)
                    if (obj)
                        stage.dialog.beginTextEdit(obj)
                }
            }

            // ---- drawing a shape point by point: a click puts a
            // point, a double click or Enter ends it, a click on its first
            // point closes it, Escape drops it. A line ends at its second.
            Item {
                id: drawLayer
                objectName: "stageDraw"
                anchors.fill: parent
                z: 60
                visible: stage.dialog.drawTool.length > 0
                focus: visible
                /// [x, y] in canvas px.
                property var points: []
                property point pointer: Qt.point(-1, -1)
                readonly property real nearPx: 10
                // Points are for the slide they were put on.
                readonly property int slideIndex: stage.edit && stage.dialog.isFreeform ? stage.edit.slides.index : -1
                onSlideIndexChanged: if (drawLayer.visible) stage.dialog.startDrawing("")
                onVisibleChanged: {
                    drawLayer.points = []
                    drawPreview.requestPaint()
                }
                function finish(closed) {
                    const pts = drawLayer.points
                    drawLayer.points = []
                    stage.dialog.finishDrawing(pts, closed)
                }
                function nearFirst(x, y) {
                    if (drawLayer.points.length < 3)
                        return false
                    const first = drawLayer.points[0]
                    return Math.hypot(first[0] * stage.stageScale - x, first[1] * stage.stageScale - y)
                           < drawLayer.nearPx
                }
                Keys.onPressed: function (event) {
                    if (event.key === Qt.Key_Escape) {
                        drawLayer.points = []
                        stage.dialog.startDrawing("")
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        drawLayer.finish(false)
                        event.accepted = true
                    }
                }
                Canvas {
                    id: drawPreview
                    anchors.fill: parent
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        const pts = drawLayer.points
                        if (!pts.length)
                            return
                        const s = stage.stageScale
                        ctx.strokeStyle = Style.canvasGuide
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(pts[0][0] * s, pts[0][1] * s)
                        for (let i = 1; i < pts.length; ++i)
                            ctx.lineTo(pts[i][0] * s, pts[i][1] * s)
                        if (drawLayer.pointer.x >= 0)
                            ctx.lineTo(drawLayer.pointer.x, drawLayer.pointer.y)
                        ctx.stroke()
                        ctx.fillStyle = Style.canvasGuide
                        for (let j = 0; j < pts.length; ++j)
                            ctx.fillRect(pts[j][0] * s - 3, pts[j][1] * s - 3, 6, 6)
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.CrossCursor
                    onPositionChanged: function (mouse) {
                        drawLayer.pointer = Qt.point(mouse.x, mouse.y)
                        drawPreview.requestPaint()
                    }
                    onExited: {
                        drawLayer.pointer = Qt.point(-1, -1)
                        drawPreview.requestPaint()
                    }
                    onClicked: function (mouse) {
                        drawLayer.forceActiveFocus()
                        if (drawLayer.nearFirst(mouse.x, mouse.y)) {
                            drawLayer.finish(true)
                            return
                        }
                        const s = stage.stageScale
                        drawLayer.points = drawLayer.points.concat([[mouse.x / s, mouse.y / s]])
                        drawPreview.requestPaint()
                        if (stage.dialog.drawTool === "line" && drawLayer.points.length === 2)
                            drawLayer.finish(false)
                    }
                    onDoubleClicked: drawLayer.finish(false)
                }
            }

            // ---- the slide's builds, played as an output plays them; a
            // click stops it
            BuildPreviewItem {
                objectName: "stageBuildPreview"
                anchors.fill: parent
                z: 50
                visible: !!(stage.dialog.isFreeform && stage.edit && stage.edit.animations.playing)
                // Only while it plays: the presenter is made then, and the view
                // has nothing to draw otherwise.
                animations: visible ? stage.edit.animations : null
                MouseArea {
                    anchors.fill: parent
                    onClicked: stage.edit.animations.stop()
                }
            }
        }
    }
}
