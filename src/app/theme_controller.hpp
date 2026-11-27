#pragma once

// The theme library of every output, and the images that preview it.
//
// Themes live on the outputs: CRUD and category defaults go through the
// output's own list and persist with it. This object owns what is only about
// showing them - the designer's draft frame, the gallery thumbnail cache, and
// the revisions QML binds to so thumbs refresh.
//
// Themes live on the session's outputs. Edits change them in place, save them
// through the hook the application gives (set_save_outputs), and then emit
// outputsEdited, on which the application rebuilds what is live. A file a theme
// no longer names is deleted only once the outputs without it are saved. Owned
// theme assets go under the open library's media folder; previews render with
// the shared PreviewResources.

#include "file_importer.hpp"
#include "image_lru_cache.hpp"
#include "preview_job.hpp"
#include "open_library.hpp"
#include "preview_resources.hpp"

#include <seventhslide/geom/canvas.hpp>
#include <seventhslide/scene/scene_frame.hpp>
#include <seventhslide/scene/theme_draft_preview.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::theme {
struct PendingAssetCopy;
}

namespace seventhslide::app {

class ElementDraftSource;

class ThemeController : public QObject {
    Q_OBJECT
    QML_ANONYMOUS

    /// Bumps when a theme library changes (CRUD, defaults, owned assets). QML
    /// theme pickers and galleries bind this so lists and thumbs refresh.
    Q_PROPERTY(int libraryRevision READ libraryRevision NOTIFY libraryChanged)
    /// Each theme library an output has, in order: ``[{kind, label}]``
    /// (``song``, ``bible``, ``media``, ``bg``).
    Q_PROPERTY(QVariantList kinds READ kinds CONSTANT)
    Q_PROPERTY(int draftRevision READ draftRevision NOTIFY draftChanged)
    /// A background file is being copied into a theme.
    Q_PROPERTY(bool copyingAsset READ copyingAsset NOTIFY assetCopyChanged)
    /// 0 to 1 across every background copy still running.
    Q_PROPERTY(double assetCopyProgress READ assetCopyProgress NOTIFY assetCopyChanged)

public:
    ThemeController(session::Session& session,
                    OpenLibrary& library,
                    PreviewResources& previews,
                    QObject* parent = nullptr);

    /// A designer whose draft elements are folded into a theme before it is
    /// painted or saved. Each is asked; the one with the theme open answers.
    void add_designer(const ElementDraftSource* designer);

    /// Themes for ``outputName`` and kind (``song``|``bible``|``media``|``bg``).
    /// Each map: id, name, tags, style, ...
    Q_INVOKABLE QVariantList list(const QString& output_name, const QString& kind);
    Q_INVOKABLE QVariantMap categoryDefaults(const QString& output_name);
    /// Create a theme; ``duplicateFromId`` copies style / tags. Returns the
    /// theme map or {error}.
    Q_INVOKABLE QVariantMap create(const QString& output_name,
                                   const QString& kind,
                                   const QString& name,
                                   const QVariantMap& style = {},
                                   const QString& tags = {},
                                   const QString& duplicate_from_id = {});
    Q_INVOKABLE QString remove(const QString& output_name,
                               const QString& kind,
                               const QString& theme_id);
    /// Make ``theme_id`` the default ``field`` (``theme``|``bg``) of ``category``
    /// (``song``|``bible``|``media``). "" when done, else why not.
    Q_INVOKABLE QString setDefault(const QString& output_name,
                                   const QString& category,
                                   const QString& field,
                                   const QString& theme_id);
    /// The choices a theme map for ``scope`` makes, grouped by category, in
    /// order: ``[{category, label, slots: [{category, field, kind, label}]}]``.
    /// ``scope`` is ``service`` (every category) or a content kind's name
    /// (``song``: a song's theme chips). A slot's ``kind`` is the theme library
    /// it picks from (one of ``kinds``). Nothing for a scope it does not know.
    Q_INVOKABLE QVariantList pickSlots(const QString& scope) const;
    /// The defaults a theme of ``kind`` can be, in order:
    /// ``[{category, field, label}]``: a text theme its own category's, a
    /// background any category's that takes one.
    Q_INVOKABLE QVariantList defaultSlots(const QString& kind) const;
    [[nodiscard]] QVariantList kinds() const;
    /// The tag rule (theme::normalize_theme_tags) for QML, so the chips show
    /// exactly what is stored. ``raw`` is a comma-separated string or a list.
    Q_INVOKABLE QStringList normalizeTags(const QVariant& raw) const;

    // For C++ alone (the slide editor, and tests): QML does not call these.

    /// One theme, as ``list`` gives each; {} when there is none, {error}
    /// when it could not be read.
    [[nodiscard]] QVariantMap theme_map(const QString& output_name,
                                        const QString& kind,
                                        const QString& theme_id);
    /// Persist name / style / tags. "" when kept, else why not.
    [[nodiscard]] QString update_theme(const QString& output_name,
                                       const QString& kind,
                                       const QString& theme_id,
                                       const QString& name,
                                       nlohmann::json style,
                                       const QString& tags = {},
                                       bool apply_tags = true);
    /// Copy a local file into an owned theme asset slot (bg | bg_video) and
    /// point the slot at it. The copy runs off the GUI thread: this returns a
    /// request id at once, and ``assetUploadFinished`` reports that id exactly
    /// once, from the event loop. The theme is unchanged until then.
    /// ``draft``: the file is copied in but the saved theme is left as it
    /// is; ``assetDrafted`` gives its URL for an editor's unsaved theme, and
    /// ``prune_assets`` takes it away again if that is not saved.
    qint64 upload_asset(const QString& output_name,
                        const QString& theme_id,
                        const QString& slot,
                        const QString& local_path,
                        bool draft = false);
    /// Delete the background files the saved theme owns but does not use.
    void prune_assets(const QString& output_name, const QString& theme_id);
    /// No one wants draft upload ``request_id`` any more (its editor closed,
    /// or a later pick replaced it): its copy is thrown away, not put in place.
    void drop_draft_upload(qint64 request_id);
    /// The theme-wide settings ``style`` sets (behaviour, background fill and
    /// fades), each one it leaves out at the default playback reads, keyed as
    /// the style names them. The slide editor shows these
    /// (``slideEdit.theme.settings``).
    [[nodiscard]] QVariantMap style_settings(const nlohmann::json& style) const;

    /// Push the slide editor's draft style into the themepreview image
    /// provider. ``kind``: text|bg. ``context``: song|bible|video|image|...|bg.
    /// ``suppress_id``: the element the editor is drawing itself (a text box
    /// being typed into).
    void update_draft_preview(const QString& output_name,
                              const QString& kind,
                              const QString& context,
                              nlohmann::json style,
                              const QString& suppress_id = {});

    [[nodiscard]] int libraryRevision() const noexcept { return previews_.theme_revision(); }
    [[nodiscard]] int draftRevision() const noexcept { return draft_revision_; }
    /// The frame the designer stage paints (``render_draft_preview``): the
    /// design with its sample body and stand-ins, as last pushed. GUI thread;
    /// the next ``update_draft_preview`` replaces it.
    [[nodiscard]] const scene::SceneFrame& draft_frame() const noexcept { return draft_frame_; }

    /// Thumbnail for ``image://themepreview/draft?...`` (GUI thread): the
    /// designer stage's whole design, each element at its own place in the
    /// stack. The stage paints the transparency grid under it, and only
    /// chrome over it.
    [[nodiscard]] QImage render_draft_preview(const QSize& size) const;
    /// Gallery / draft router for ``image://themepreview/<id>``. ``id`` is
    /// ``draft?...``, ``grid?...``, or
    /// ``theme/<output>/<kind>/<themeId>?r=<rev>[&c=<ctx>]`` (``ctx`` one of the
    /// kind's contexts; the kind's first by default). Gallery thumbs are
    /// LRU-cached; ``draft`` / ``grid`` are never cached. Text-theme thumbs
    /// isolate typography on a checkerboard.
    [[nodiscard]] QImage render_preview(const QString& id, const QSize& size) const;
    /// ``render_preview``, prepared here and painted by whoever finishes it
    /// (see preview_job.hpp). GUI thread.
    [[nodiscard]] PreviewJob prepare_preview(const QString& id, const QSize& size) const;

    /// Whether library file ``path`` ("<folder>/<name>") is not here but on its
    /// way from another computer (``MediaController::on_its_way``).
    void set_on_its_way(std::function<bool(const std::string& path)> on_its_way)
    {
        on_its_way_ = std::move(on_its_way);
    }

    /// Saves the session's outputs, as they are now, to the open library;
    /// throws when it cannot. Every edit saves through it before
    /// ``outputsEdited``.
    void set_save_outputs(std::function<void()> save) { save_outputs_ = std::move(save); }

    /// A theme library changed underneath - here, or by a profile switch.
    /// Drops the cached thumbs and bumps ``libraryRevision``.
    void library_changed();
    /// Forget cached thumbs without announcing a change (the library closed).
    void clear_previews() { preview_cache_.clear(); }
    /// Stop every background copy: the library they were for is closing.
    /// Each upload still reports, as stopped.
    void cancel_asset_copies() { asset_copier_.cancel_all(); }

    [[nodiscard]] bool copyingAsset() const noexcept { return asset_copier_.busy(); }

    [[nodiscard]] double assetCopyProgress() const noexcept { return asset_copier_.progress(); }

signals:
    void libraryChanged();
    void draftChanged();
    /// A call failed with no return value to say so (see entry_error.hpp);
    /// SessionFacade forwards it to the shell.
    void errorOccurred(const QString& message);
    /// The upload ``uploadAsset`` returned ``requestId`` for is over. ``error``
    /// is empty when the slot now shows the file.
    void assetUploadFinished(qint64 requestId, const QString& error);
    /// A draft upload's file is in place at ``url``; just before it finishes.
    void assetDrafted(qint64 requestId, const QString& url);
    void assetCopyChanged();
    /// A theme on an output was edited in place and the outputs saved: the
    /// application rebuilds what is live.
    void outputsEdited();

private:
    /// ``render_draft_preview``, prepared (see preview_job.hpp).
    [[nodiscard]] PreviewJob prepare_draft_preview(const QSize& size) const;
    [[nodiscard]] const model::OutputConfig* find_output(const QString& name) const;
    [[nodiscard]] model::OutputConfig* find_output_mutable(const QString& name);
    /// Save the outputs through ``save_outputs_``; throws when that fails.
    void save_outputs() const;
    /// ``save_outputs`` after an edit of ``oc``. Empty when saved; otherwise
    /// why not, with ``oc`` put back as ``before``.
    [[nodiscard]] QString save_or_put_back(model::OutputConfig& oc, model::OutputConfig before);

    /// Theme ``theme_id`` on ``output_name`` shares a background another theme
    /// owns and the filesystem could not link it: copy it in the background.
    void copy_shared_assets(const QString& output_name,
                            const std::string& theme_id,
                            const std::vector<theme::PendingAssetCopy>& copies);
    /// Move a copied file into place as ``slot``'s owned file and persist.
    /// ``expected_url``, when set, is the URL the slot must still hold.
    /// Empty return = ok. Removes the staged file whatever happens. With
    /// ``drafted`` the file is put in place but not on the theme: its URL
    /// goes there.
    [[nodiscard]] QString commit_asset(const QString& output_name,
                                       const std::string& theme_id,
                                       const std::string& slot,
                                       const std::string& expected_url,
                                       const ImportBatch& batch,
                                       std::string* drafted = nullptr);

    /// Why ``theme`` cannot be saved yet: a background it would share with
    /// another theme is on its way, so it cannot have its own copy, and would
    /// be left on the other's for good. Those in ``held`` it shared already.
    /// Empty when it can.
    [[nodiscard]] QString why_still_coming(const model::ThemeEntry& theme,
                                           const std::vector<std::string>& held) const;

    session::Session& session_;
    OpenLibrary& library_;
    PreviewResources& previews_;
    std::function<bool(const std::string& path)> on_its_way_;
    std::function<void()> save_outputs_;
    void apply_designer_elements(nlohmann::json& style) const;

    std::vector<const ElementDraftSource*> designers_;
    int draft_revision_ = 0;
    scene::SceneFrame draft_frame_{};
    /// The canvas the draft was laid out on: its preview's shape.
    geom::CanvasSize draft_canvas_ = geom::k_reference_canvas;
    /// What the draft frame was last built from: the same again (an edit's
    /// own refresh and the editor's after it) builds nothing.
    nlohmann::json draft_inputs_;
    /// Rendered gallery thumbs by key; cleared whenever a theme changes.
    mutable ImageLruCache preview_cache_{128};
    qint64 last_upload_id_ = 0;
    /// Draft uploads still copying whose file is not to be kept.
    std::set<qint64> dropped_drafts_;
    /// Last, so it is destroyed first: its worker is stopped and joined before
    /// anything a finished copy would touch goes away.
    FileImporter asset_copier_;
};

}  // namespace seventhslide::app
