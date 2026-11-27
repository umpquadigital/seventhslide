#pragma once

// The slide editor's session, as QML sees it: one object for a Freeform item,
// a text theme or a background theme, split into the things the window reads.
//
//   session.slideEdit          the document: what is open, its name, its
//                              canvas, save and cancel, undo and redo
//   session.slideEdit.objects  the canvas: the selection, its values, every
//                              object on the stage, and the commands on them
//   session.slideEdit.slides   a Freeform item's slides: the sorter's rows,
//                              the current slide, its background
//   session.slideEdit.cues     line control, the cues and cue-linked text
//   session.slideEdit.fields   a Freeform item's fields
//   session.slideEdit.theme    a theme's tags, settings, draft and assets
//
// Both canvas editors are core's (freeform::Editor, theme::Editor); the
// session holds one of each and drives whichever the open document uses.
// Every command is one call into core, then the change signals. Values go to
// QML by the names the property tables use (snake_case), as they paint, so a
// control reads by the name it writes.
//
// SessionFacade owns one and exposes it as `session.slideEdit`.

#include "canvas_commands.hpp"
#include "canvas_ops_qml.hpp"
#include "element_draft_source.hpp"
#include "freeform_document_store.hpp"
#include "freeform_editor_models.hpp"
#include "preview_job.hpp"
#include "preview_resources.hpp"

#include <seventhslide/build/slide_builder.hpp>
#include <seventhslide/freeform/editor.hpp>
#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/session/session.hpp>
#include <seventhslide/theme/element_editor.hpp>
#include <seventhslide/theme/theme_kind.hpp>

#include <nlohmann/json.hpp>

#include <QtQml/qqmlregistration.h>
#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::render {
class ScenePresenter;
}

namespace seventhslide::app {

class BackgroundThemeCopier;
class MediaController;

class SlideEditSession;
class ThemeController;

/// What a document is.
enum class SlideEditMode {
    None,
    Freeform,
    TextTheme,
    BackgroundTheme,
};

/// The canvas: the selection, the objects on the stage, and the commands on
/// them. Those the two editors share (picking, undo, gestures, the
/// clipboard, arranging) go through CanvasCommands to whichever is open.
class SlideObjectsSession final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Part of the slide editor's session: session.slideEdit")
    // What is selected, and what can be done with it. Edits change these too.
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(bool canRaise READ canRaise NOTIFY selectionChanged)
    Q_PROPERTY(bool canLower READ canLower NOTIFY selectionChanged)
    Q_PROPERTY(bool canGroup READ canGroup NOTIFY selectionChanged)
    Q_PROPERTY(bool canUngroup READ canUngroup NOTIFY selectionChanged)
    Q_PROPERTY(bool selectionLocked READ selectionLocked NOTIFY selectionChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged)
    /// The first selected object's values, or a new object's with nothing
    /// selected: what the ribbon and the sidebar show.
    Q_PROPERTY(QVariantMap selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedType READ selectedType NOTIFY selectionChanged)
    /// Which controls change how the selected object paints, by
    /// theme::ObjectControls' names (runs, weight, font, color, align, fit,
    /// shape, clock, indicator, effects, fill); all off with nothing selected.
    Q_PROPERTY(QVariantMap supports READ supports NOTIFY selectionChanged)
    /// The span each number property of the selection is held to, by its
    /// name in ``selection``: {min, max}, in the units ``selection`` reads
    /// (freeform::object_property_ranges, theme::element_property_ranges).
    Q_PROPERTY(QVariantMap ranges READ ranges NOTIFY selectionChanged)
    /// The room the selected text box's chord labels take over each line, in
    /// canvas px, as the output makes it (layout::chord_row_px); 0 for
    /// anything but a Freeform text box.
    Q_PROPERTY(int chordRoom READ chordRoom NOTIFY selectionChanged)
    /// Every object on the stage, back to front, with the text each shows.
    /// Its own signal, sent once the list is rebuilt: QML hears
    /// documentChanged before the slot that rebuilds it runs.
    Q_PROPERTY(QVariantList canvasObjects READ canvasObjects NOTIFY canvasObjectsChanged)
    /// The same, as rows kept by object id: what the stage draws from.
    Q_PROPERTY(QAbstractListModel* canvasModel READ canvasModel CONSTANT)

public:
    explicit SlideObjectsSession(SlideEditSession& owner);

    // --- selection ------------------------------------------------------
    // With no document open there is nothing to pick: these do nothing.
    Q_INVOKABLE void selectObject(const QString& object_id, bool additive = false);
    Q_INVOKABLE void selectIds(const QStringList& object_ids, bool additive = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();

    // Every edit below returns "" on success, else why it was refused; the
    // selection afterwards is ``selectedIds``.

    // --- objects --------------------------------------------------------
    /// A new object, selected. ``type``: text | image | shape | indicator |
    /// clock. ``options``: filename (image), shapeKind (shape), binding (a
    /// theme element's: lyrics, bible_text, indicator, countdown, video_area,
    /// image_area, web_area; static otherwise).
    Q_INVOKABLE QString insert(const QString& type, const QVariantMap& options = {});
    /// A shape drawn through ``points`` (each [x, y] in canvas px): a curve
    /// through them when ``smooth``, joined back to the first when
    /// ``closed``. A Freeform item's only.
    Q_INVOKABLE QString draw(const QVariantList& points, bool smooth, bool closed);
    Q_INVOKABLE QString deleteSelected();
    Q_INVOKABLE QString reorderSelected(seventhslide::app::CanvasOrderQml::Order order);
    Q_INVOKABLE QString copySelected();
    Q_INVOKABLE QString paste();

    // --- history --------------------------------------------------------
    /// Step the document back / forward one command.
    Q_INVOKABLE QString undo();
    Q_INVOKABLE QString redo();
    /// Everything between the two is one undo step (a drag, a resize).
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();

    // --- geometry and arrangement --------------------------------------
    Q_INVOKABLE QString setGeometries(const QVariantList& geometries);
    Q_INVOKABLE QString align(seventhslide::app::CanvasAlignQml::Align align);
    Q_INVOKABLE QString distribute(seventhslide::app::CanvasDistributeQml::Distribute dist);
    Q_INVOKABLE QString nudge(double dx, double dy);
    Q_INVOKABLE QString flip(seventhslide::app::CanvasFlipQml::Axis axis);
    Q_INVOKABLE QString group();
    Q_INVOKABLE QString ungroup();
    Q_INVOKABLE QString setLocked(bool locked);

    // --- properties and text --------------------------------------------
    /// Set a property of the selected object by the design ribbon's name
    /// (``font_size``, ``fill``, ``x``, ...). Empty on success, else the reason.
    Q_INVOKABLE QString setSelectedProperty(const QString& name, const QVariant& value);
    /// In-canvas typing committed: markup plus the box's B/I/U.
    Q_INVOKABLE QString setText(const QString& object_id, const QString& text, bool bold,
                                bool italic, bool underline);
    /// Whole-box run style (B/I/U, size, colour, family) on the selection.
    Q_INVOKABLE QString applyTextAttrs(const QVariantMap& attrs);
    /// ``type``: cut | fade | fade_through_black.
    Q_INVOKABLE QString setTransition(const QString& type, int duration_ms);
    /// ``text`` (markup) as styled lines for the canvas editor, each run's
    /// style explicit.
    Q_INVOKABLE QVariantList parseRichLines(const QString& text, bool bold, bool italic,
                                            bool underline) const;
    /// The wall clock as a clock object reads it now.
    Q_INVOKABLE QString clockText(bool clock_24h, bool show_seconds) const;

    // --- state ----------------------------------------------------------
    [[nodiscard]] QStringList selectedIds() const;
    [[nodiscard]] bool canRaise() const;
    [[nodiscard]] bool canLower() const;
    [[nodiscard]] bool canGroup() const;
    [[nodiscard]] bool canUngroup() const;
    [[nodiscard]] bool selectionLocked() const;
    [[nodiscard]] bool canPaste() const;
    [[nodiscard]] QVariantMap selection() const;
    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] QString selectedType() const;
    [[nodiscard]] QVariantMap supports() const;
    [[nodiscard]] QVariantMap ranges() const;
    [[nodiscard]] int chordRoom() const;
    [[nodiscard]] QVariantList canvasObjects() const { return canvas_objects_; }
    [[nodiscard]] QAbstractListModel* canvasModel() noexcept { return &canvas_model_; }

signals:
    /// The document or the view of it changed: an edit, undo / redo, opening
    /// or closing. ``selectionChanged`` follows, since what the selection can
    /// do may have.
    void documentChanged();
    /// The selection, or what it can do, changed. A click emits only this.
    void selectionChanged();
    void clipboardChanged();
    /// A call failed with no return value to say so (see entry_error.hpp);
    /// SessionFacade forwards it to the shell.
    void errorOccurred(const QString& message);
    void canvasObjectsChanged();

private:
    friend class SlideEditSession;

    /// What an edit can have changed, for the slide list.
    enum class Touched {
        /// Anything in the document.
        Anything,
        /// Only where objects sit on the current canvas: moved, resized,
        /// aligned or flipped.
        Geometry,
    };

    /// Drive another editor from now on (the one the document open uses).
    void rebind(CanvasCommands& canvas) noexcept { canvas_ = &canvas; }
    /// ``result`` as QML sees it: on success the slide list follows, then
    /// the change signals and ""; else why it was refused.
    QString apply(const freeform::EditResult& result, Touched touched = Touched::Anything);
    /// A clipboard command's ``result``: "" and ``clipboardChanged`` on success.
    QString apply_copy(const freeform::EditResult& result);
    /// Everything may have changed: opening or closing a document.
    void emit_all();
    /// Run ``change`` on the picked objects and announce the selection. A
    /// closed editor has nothing to pick, so it does neither.
    template <class Change>
    void select_while_active(Change change);
    /// Built once per document change, for the list and the model both.
    [[nodiscard]] QVariantList build_canvas_objects() const;

    SlideEditSession& owner_;
    CanvasCommands* canvas_;
    QVariantList canvas_objects_;
    CanvasObjectModel canvas_model_;
};

/// A Freeform item's slides.
class SlideSlidesSession final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Part of the slide editor's session: session.slideEdit")
    /// The sorter's rows: slides, and under line control the cues of each.
    Q_PROPERTY(seventhslide::app::FreeformSlideListModel* list READ list CONSTANT)
    Q_PROPERTY(QString currentId READ currentId NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    /// The current slide's background: kind (none | color | image | video),
    /// color, image, video, transition_type (cut | fade | fade_through_black:
    /// a cut when unset), transition_ms; and the slide's own transition,
    /// slide_transition_type and slide_transition_ms.
    Q_PROPERTY(QVariantMap background READ background NOTIFY changed)
    /// The current slide's speaker notes.
    Q_PROPERTY(QString notes READ notes NOTIFY changed)

public:
    explicit SlideSlidesSession(SlideEditSession& owner);

    /// Pick slide ``index`` alone.
    Q_INVOKABLE void select(int index);
    /// Pick the slides from the one last picked alone to ``index``.
    Q_INVOKABLE void extendTo(int index);
    Q_INVOKABLE void selectAll();
    /// How many slides are picked.
    Q_INVOKABLE int selectedCount() const;
    Q_INVOKABLE QString insert();
    Q_INVOKABLE QString duplicate();
    Q_INVOKABLE QString remove();
    Q_INVOKABLE QString reorder(int from, int to);
    /// Copy the slides picked, with their lines; Paste puts them after the
    /// last slide picked.
    Q_INVOKABLE QString copy();
    /// The sorter's drop of a slide thumb under cue ``last_cue``.
    Q_INVOKABLE QString placeThumb(int slide_index, int last_cue);
    /// ``kind``: none | color | image | video; ``value`` the colour, or the
    /// picture's or video's library file.
    Q_INVOKABLE QString setBackground(const QString& kind, const QString& value);
    /// Copy background theme ``theme_id`` into the current slide (plan
    /// 4.2.1): its colour now, its picture or video once the library has a
    /// copy, with its fade. Nothing links the slide to the theme afterwards.
    Q_INVOKABLE QString copyBackgroundTheme(const QString& theme_id);
    /// ``type``: cut | fade | fade_through_black.
    Q_INVOKABLE QString setBackgroundTransition(const QString& type, int duration_ms);
    /// How the current slide comes in, or every slide with ``all``. The first
    /// slide's is the item's entrance.
    /// ``direction`` (right | left | up | down) is the side a moving one
    /// comes from.
    Q_INVOKABLE QString setSlideTransition(const QString& type, int duration_ms, bool all = false,
                                           const QString& direction = {});
    /// The current slide's speaker notes.
    Q_INVOKABLE QString setNotes(const QString& notes);
    /// The outputs whose deck can be edited, by name, in the outputs' own
    /// order: the one edited and every other on with a deck. A menu of them
    /// keeps its order whichever is picked.
    Q_INVOKABLE QStringList deckOutputs() const;

    [[nodiscard]] FreeformSlideListModel* list() noexcept { return &list_; }
    [[nodiscard]] QString currentId() const;
    [[nodiscard]] int index() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] QVariantMap background() const;
    [[nodiscard]] QString notes() const;
    /// Bumped whenever a thumbnail or the preview may show something else.
    /// For tests: a view's thumbs carry their own revision in their URLs.
    [[nodiscard]] int preview_revision() const noexcept { return preview_revision_; }

    /// Editor-draft thumbnail (not the live session). Empty if nothing is
    /// open. ``output`` is an output id picking the deck (empty = the one
    /// being edited); ``slide_id`` selects by stable id when set.
    [[nodiscard]] QImage render_preview(const QString& output, int slide_index, const QSize& size,
                                        const QString& slide_id = {}) const;
    [[nodiscard]] PreviewJob prepare_preview(const QString& output, int slide_index,
                                             const QSize& size, const QString& slide_id = {}) const;

signals:
    void changed();

private:
    friend class SlideEditSession;
    friend class SlideAnimationsSession;
    /// Rebuild the rows. True when a thumbnail changed.
    bool reload();
    bool refresh_current_slide();
    void invalidate_preview();
    void ensure_preview_build() const;

    SlideEditSession& owner_;
    FreeformSlideListModel list_;
    mutable build::SlideBuildResult preview_build_;
    mutable bool preview_build_dirty_ = true;
    int preview_revision_ = 0;
};

/// Line control, the cues and cue-linked text.
class SlideCuesSession final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Part of the slide editor's session: session.slideEdit")
    Q_PROPERTY(bool lineControl READ lineControl NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    /// The selected cue: text, verse_type, verse_number, chapter, verse,
    /// show_ref. Empty with none.
    Q_PROPERTY(QVariantMap current READ current NOTIFY changed)
    Q_PROPERTY(QStringList verseTypes READ verseTypes CONSTANT)
    Q_PROPERTY(bool canDelete READ canDelete NOTIFY changed)
    Q_PROPERTY(bool canDisableLineControl READ canDisableLineControl NOTIFY changed)

public:
    explicit SlideCuesSession(SlideEditSession& owner);

    Q_INVOKABLE void select(int index);
    Q_INVOKABLE QString setLineControl(bool on, bool flatten = false);
    Q_INVOKABLE QString insert(const QString& text = {});
    Q_INVOKABLE QString remove();
    Q_INVOKABLE QString rename(const QString& text);
    Q_INVOKABLE QString setVerse(const QString& type, int number);
    Q_INVOKABLE QString setScripture(int chapter, int verse);
    Q_INVOKABLE QString setRefShown(bool shown);
    /// The selected text box shows the slide's cues, or its own text again.
    Q_INVOKABLE QString setLinked(bool on);
    Q_INVOKABLE QString setLinkedStyle(const QString& markup);
    Q_INVOKABLE QString linkedEditMarkup() const;
    Q_INVOKABLE QVariantMap linkedEditInfo() const;
    Q_INVOKABLE QString setLinkedEdit(const QString& markup, int upcoming, const QVariantList& cues);
    Q_INVOKABLE bool linkedEditAllowed(const QString& plain, int upcoming) const;
    Q_INVOKABLE QString setShowUpcoming(bool on);

    [[nodiscard]] bool lineControl() const;
    [[nodiscard]] int index() const;
    [[nodiscard]] QVariantMap current() const;
    [[nodiscard]] QStringList verseTypes() const;
    [[nodiscard]] bool canDelete() const;
    [[nodiscard]] bool canDisableLineControl() const;

signals:
    void changed();

private:
    SlideEditSession& owner_;
};

/// The current slide's builds: its animations in order, what the
/// Animations tab and the Build pane edit, and a preview that plays them.
class SlideAnimationsSession final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Part of the slide editor's session: session.slideEdit")
    /// In the slide's order: {objectId, order, index, effect, kind (entrance
    /// | emphasis | exit), trigger, direction, durationMs, delayMs, name,
    /// step (the on-click step it starts, from 1; 0 for one that is not on a
    /// click), summary}.
    Q_PROPERTY(QVariantList list READ list NOTIFY changed)
    /// The preview is playing the slide's builds.
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)

public:
    explicit SlideAnimationsSession(SlideEditSession& owner);
    ~SlideAnimationsSession() override;

    [[nodiscard]] QVariantList list() const;
    /// ``effect`` (fade, fly_in, ... fade_out) on each selected object.
    Q_INVOKABLE QString add(const QString& effect);
    /// ``field``: effect | trigger | direction | duration_ms | delay_ms.
    Q_INVOKABLE QString set(const QString& object_id, int order, const QString& field,
                            const QVariant& value);
    Q_INVOKABLE QString remove(const QString& object_id, int order);
    /// The animation at ``from`` in the slide's order, to ``to``.
    Q_INVOKABLE QString move(int from, int to);

    /// Play the current slide as the output would: it comes in, then each
    /// step's builds, one after another (BuildPreviewItem shows it).
    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    [[nodiscard]] bool playing() const noexcept { return playing_; }
    /// What the preview paints from; made the first time it is asked for. It
    /// draws pictures from the previews' image cache, so from the library
    /// open now.
    [[nodiscard]] render::ScenePresenter* presenter();

signals:
    void changed();
    void playingChanged();

private:
    friend class SlideEditSession;
    /// Step ``step`` of the preview, then the next once it has played.
    void play_step(int step, std::uint64_t run);
    SlideEditSession& owner_;
    std::unique_ptr<render::ScenePresenter> presenter_;
    bool playing_ = false;
    std::uint64_t run_ = 0;
};

/// A Freeform item's fields.
class SlideFieldsSession final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(seventhslide::app::FreeformFieldListModel* list READ list CONSTANT)

public:
    explicit SlideFieldsSession(SlideEditSession& owner);

    Q_INVOKABLE QString set(const QString& name, const QString& value);
    Q_INVOKABLE QString add(const QString& name);
    Q_INVOKABLE QString rename(const QString& from, const QString& to);
    Q_INVOKABLE QString remove(const QString& name);
    /// ``raw`` as a field name: lowercased, spaces to ``_``, illegal
    /// characters dropped; empty when nothing legal is left.
    Q_INVOKABLE QString sanitize(const QString& raw) const;
    Q_INVOKABLE QStringList names() const;
    /// How many places use ``{name}`` or ``{-name}``.
    Q_INVOKABLE int usage(const QString& name) const;
    /// A text theme's tokens: what a service fills in (``song-title``,
    /// ``authors``, ``copyright``, ...), which the Fields window lists in place
    /// of fields. Empty for a Freeform item, whose fields are its own.
    Q_INVOKABLE QStringList tokenNames() const;

    [[nodiscard]] FreeformFieldListModel* list() noexcept { return &list_; }

signals:
    void changed();

private:
    friend class SlideEditSession;
    SlideEditSession& owner_;
    FreeformFieldListModel list_;
};

/// A theme's tags, theme-wide settings, draft preview and owned assets.
class SlideThemeSession final : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    /// text | bg.
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(QString themeId READ themeId NOTIFY changed)
    Q_PROPERTY(QStringList tags READ tags NOTIFY changed)
    /// The theme-wide settings as playback reads them (behaviour, the
    /// background fill and fades), keyed as the style names them.
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    /// The span each number in ``settings`` is held to, by its key: {min,
    /// max} (theme::theme_behavior_ranges).
    Q_PROPERTY(QVariantMap settingRanges READ settingRanges CONSTANT)
    /// What Insert offers for the context: {id, label, icon, type, binding,
    /// menu}, ``menu`` naming a palette the entry picks from.
    Q_PROPERTY(QVariantList insertKinds READ insertKinds NOTIFY changed)

public:
    explicit SlideThemeSession(SlideEditSession& owner);

    Q_INVOKABLE void setTags(const QStringList& tags);
    Q_INVOKABLE void setSetting(const QString& key, const QVariant& value);
    /// The context being edited's transition (``type``: cut | fade |
    /// fade_through_black | push | wipe | split | zoom | cover | reveal; its
    /// length; ``direction`` as setSlideTransition's): each content type's is
    /// its own. Titles is the Song's.
    Q_INVOKABLE void setContextTransition(const QString& type, int duration_ms,
                                          const QString& direction = {});
    /// Paint the draft again (session.themes.draftRevision follows).
    /// ``suppress_id`` is the box being typed into, left out of the raster
    /// until a call names another or none; the session's own repaints (an
    /// edit, a setting) keep leaving it out.
    Q_INVOKABLE void refreshDraft(const QString& suppress_id = {});
    /// Copy a local file into the theme's ``slot`` (bg | bg_video); the draft
    /// takes it when the copy is done.
    Q_INVOKABLE void uploadAsset(const QString& slot, const QString& local_path);
    Q_INVOKABLE QString clearAsset(const QString& slot);

    [[nodiscard]] QString kind() const;
    [[nodiscard]] QString themeId() const;
    [[nodiscard]] QStringList tags() const;
    [[nodiscard]] QVariantMap settings() const;
    [[nodiscard]] QVariantMap settingRanges() const;
    [[nodiscard]] QVariantList insertKinds() const;

signals:
    void changed();

private:
    friend class SlideEditSession;
    void on_asset_upload_finished(qint64 request_id, const QString& error);
    /// ``url`` (empty: none) into the draft's ``slot``.
    void put_asset_slot(const QString& slot, const QString& url);
    /// Paint the draft again, still leaving out the box being typed into.
    void repaint_draft();

    SlideEditSession& owner_;
    /// The box being typed into (``refreshDraft``), which QML draws.
    QString suppress_id_;
    qint64 pending_upload_ = -1;
    QString pending_slot_;
    /// Where the pending upload's file landed, once it has.
    QString pending_url_;
};

class SlideEditSession : public QObject, public ElementDraftSource {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The session owns the slide editor: session.slideEdit")
    /// "" | freeform | text | bg.
    Q_PROPERTY(QString mode READ mode NOTIFY documentChanged)
    Q_PROPERTY(bool active READ active NOTIFY documentChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY documentChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY documentChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY documentChanged)
    Q_PROPERTY(QString name READ name NOTIFY documentChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY documentChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY documentChanged)
    /// The output being edited (a Freeform deck's), or the theme's output.
    Q_PROPERTY(QString output READ output NOTIFY documentChanged)
    /// A theme's design context: song | bible | media | titles | bg.
    Q_PROPERTY(QString context READ context NOTIFY documentChanged)
    Q_PROPERTY(QStringList contexts READ contexts NOTIFY documentChanged)
    /// Counts the document's changes: what QML asks of it by a call (how
    /// many fields, where one is used) reads this to follow them.
    Q_PROPERTY(int revision READ revision NOTIFY documentChanged)
    Q_PROPERTY(seventhslide::app::SlideObjectsSession* objects READ objects CONSTANT)
    Q_PROPERTY(seventhslide::app::SlideSlidesSession* slides READ slides CONSTANT)
    Q_PROPERTY(seventhslide::app::SlideCuesSession* cues READ cues CONSTANT)
    Q_PROPERTY(seventhslide::app::SlideFieldsSession* fields READ fields CONSTANT)
    Q_PROPERTY(seventhslide::app::SlideAnimationsSession* animations READ animations CONSTANT)
    Q_PROPERTY(seventhslide::app::SlideThemeSession* theme READ theme CONSTANT)

public:
    SlideEditSession(const session::Session& session,
                     PreviewResources& previews,
                     IFreeformDocumentStore& documents,
                     ThemeController& themes,
                     QObject* parent = nullptr);
    ~SlideEditSession() override;

    Q_INVOKABLE bool openFreeform(qint64 freeform_id);
    /// ``kind``: one of ``ThemeController.kinds`` (``song``, ``bible``,
    /// ``media``, ``bg``). False when the output has no such theme.
    Q_INVOKABLE bool openTheme(const QString& output_name, const QString& kind,
                               const QString& theme_id);
    Q_INVOKABLE QString setName(const QString& name);
    /// A Freeform deck by its output's name.
    Q_INVOKABLE void setOutput(const QString& name);
    Q_INVOKABLE void setContext(const QString& context);
    Q_INVOKABLE QString save();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE QString undo();
    Q_INVOKABLE QString redo();
    /// Copy a picture or video from the disk (``url``, a path or file URL)
    /// into the open Freeform item's own files, never the library's
    /// (freeform/own_media.hpp). Returns the request id
    /// ``MediaController::importFinished`` reports, its ``filenames`` the
    /// URL to name the file by; with no item open the report says why.
    Q_INVOKABLE qint64 importMedia(bool video, const QString& url);

    [[nodiscard]] QString mode() const;
    [[nodiscard]] bool active() const;
    [[nodiscard]] bool dirty() const;
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] int canvasWidth() const;
    [[nodiscard]] int canvasHeight() const;
    [[nodiscard]] QString output() const;
    [[nodiscard]] QString context() const;
    [[nodiscard]] QStringList contexts() const;
    [[nodiscard]] int revision() const noexcept { return revision_; }
    [[nodiscard]] SlideObjectsSession* objects() noexcept { return objects_.get(); }
    [[nodiscard]] SlideSlidesSession* slides() noexcept { return slides_.get(); }
    [[nodiscard]] SlideCuesSession* cues() noexcept { return cues_.get(); }
    [[nodiscard]] SlideFieldsSession* fields() noexcept { return fields_.get(); }
    [[nodiscard]] SlideAnimationsSession* animations() noexcept { return animations_.get(); }
    [[nodiscard]] SlideThemeSession* theme() noexcept { return theme_.get(); }

    /// Direct access for C++ callers and tests.
    [[nodiscard]] const freeform::Editor& freeform_editor() const noexcept { return freeform_; }
    [[nodiscard]] geom::CanvasSize canvas() const;

    // ElementDraftSource: the open text theme's elements, into ``style``.
    void apply_elements(nlohmann::json& style) const override;

    /// A theme changed under the open document: thumbnails and the preview
    /// were built with the old themes.
    void on_theme_library_changed();

    /// What copies a background theme into slides; null in tests that need
    /// none (a copy then fails, saying so).
    void set_background_copier(BackgroundThemeCopier* copier) noexcept { bg_copier_ = copier; }
    /// What copies files into the open item (``importMedia``); null in tests
    /// that need none.
    void set_media(MediaController* media) noexcept { media_ = media; }

signals:
    void documentChanged();
    void selectionChanged();
    void clipboardChanged();
    void errorOccurred(const QString& message);

private:
    friend class SlideObjectsSession;
    friend class SlideSlidesSession;
    friend class SlideCuesSession;
    friend class SlideFieldsSession;
    friend class SlideAnimationsSession;
    friend class SlideThemeSession;

    [[nodiscard]] bool freeform_mode() const noexcept { return mode_ == SlideEditMode::Freeform; }
    [[nodiscard]] bool text_theme_mode() const noexcept { return mode_ == SlideEditMode::TextTheme; }
    /// The kind of the text theme open (song, Bible or media themes).
    [[nodiscard]] theme::ThemeKind text_theme_kind() const noexcept;
    [[nodiscard]] bool theme_mode() const noexcept
    {
        return mode_ == SlideEditMode::TextTheme || mode_ == SlideEditMode::BackgroundTheme;
    }
    /// ``result`` as QML sees it: "" and the change signals, or why not.
    QString apply(const freeform::EditResult& result);
    /// Everything about the document may have changed.
    void document_changed();
    /// Count the change, then say so: every documentChanged goes through
    /// here. QML hears the signal before any C++ slot, so a slot that counted
    /// it would leave QML reading the old count.
    void emit_document_changed();
    /// Only the slide or cue picked moved: the list's selection follows, the
    /// stage shows the slide.
    void view_moved();
    /// The lists after an edit: the whole document, or one slide's look.
    void after_edit(bool geometry_only);
    void close_document();
    [[nodiscard]] std::string output_id_for(const QString& name) const;
    [[nodiscard]] QString output_name_for(std::string_view output_id) const;
    /// Copy output ``output_id``'s background theme ``theme_id`` into slide
    /// ``slide_id`` as an edit, once its files are in; nothing if the editor
    /// has moved to another document by then.
    QString copy_background_into(std::string output_id, std::string theme_id,
                                 std::string slide_id);

    const session::Session& session_;
    PreviewResources& previews_;
    IFreeformDocumentStore& documents_;
    ThemeController& themes_;

    SlideEditMode mode_ = SlideEditMode::None;
    freeform::Editor freeform_;
    theme::Editor theme_editor_;
    CanvasCommandsFor<freeform::Editor> freeform_commands_{freeform_};
    CanvasCommandsFor<theme::Editor> theme_commands_{theme_editor_};
    qint64 item_id_ = -1;
    /// Bumped whenever a document opens or closes, so a background copy that
    /// lands later knows whether it is still wanted.
    std::uint64_t opened_ = 0;
    BackgroundThemeCopier* bg_copier_ = nullptr;
    MediaController* media_ = nullptr;

    // A theme: where it lives, what it holds beside its elements.
    QString theme_output_;
    QString theme_kind_;
    QString theme_id_;
    QString theme_name_;
    QStringList theme_tags_;
    /// The style keys that are not the element model (behaviour, the
    /// background fill and fades), as the draft has them.
    nlohmann::json theme_style_ = nlohmann::json::object();
    bool theme_dirty_ = false;
    std::string theme_context_ = "song";
    int revision_ = 0;

    std::unique_ptr<SlideObjectsSession> objects_;
    std::unique_ptr<SlideSlidesSession> slides_;
    std::unique_ptr<SlideCuesSession> cues_;
    std::unique_ptr<SlideFieldsSession> fields_;
    std::unique_ptr<SlideAnimationsSession> animations_;
    std::unique_ptr<SlideThemeSession> theme_;
};

}  // namespace seventhslide::app
