#pragma once

// SessionFacade - the application's composition root and QML entry point.
//
// Owns the core Session, the text measurer, the open library's database and
// repositories, and one controller per area of the app. QML reaches each
// controller through a property below and calls it directly.
//
// The controllers borrow what they share - the session, the open library
// (OpenLibrary) and the preview resources (PreviewResources) - and announce
// what changed with signals. wire_controllers() is the one place that decides
// what follows: reloading the models after live content moved, saving the
// outputs after an edit, repainting. Only switching libraries is a request
// back to this object (ILibrarySwitcher), because only it can rebuild
// everything that belongs to one library.
//
// What stays here spans those areas: opening libraries, following live
// content to the models and the outputs (after_mutation, the present pass,
// the clock), the remote's views of the session (run_in_view), the operator
// previews, and app settings. The web remote drives the same objects
// (session_remote_bridge.cpp).

#include "library_switcher.hpp"
#include "open_library.hpp"
#include "output_preview_controller.hpp"
#include "preview_resources.hpp"
#include "live_receiver.hpp"
#include "web_renderer.hpp"

#include <seventhslide/freeform/document.hpp>
#include <seventhslide/model/app_settings.hpp>
#include <seventhslide/model/machine_settings.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/session/session.hpp>

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <array>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

// The controllers are held by pointer, so this header names them only; moc
// still needs each one to register the properties below.
Q_MOC_INCLUDE("destinations_controller.hpp")
Q_MOC_INCLUDE("font_controller.hpp")
Q_MOC_INCLUDE("freeform_library_controller.hpp")
Q_MOC_INCLUDE("library_controller.hpp")
Q_MOC_INCLUDE("library_registry_controller.hpp")
Q_MOC_INCLUDE("live_controller.hpp")
Q_MOC_INCLUDE("media_controller.hpp")
Q_MOC_INCLUDE("mixer_controller.hpp")
Q_MOC_INCLUDE("tools_controller.hpp")
Q_MOC_INCLUDE("convert_controller.hpp")
Q_MOC_INCLUDE("download_controller.hpp")
Q_MOC_INCLUDE("export_controller.hpp")
Q_MOC_INCLUDE("output_list_model.hpp")
Q_MOC_INCLUDE("output_settings_controller.hpp")
Q_MOC_INCLUDE("profile_controller.hpp")
Q_MOC_INCLUDE("service_item_model.hpp")
Q_MOC_INCLUDE("service_thumbs.hpp")
Q_MOC_INCLUDE("stack_controller.hpp")
Q_MOC_INCLUDE("theme_controller.hpp")
Q_MOC_INCLUDE("slide_edit_session.hpp")
Q_MOC_INCLUDE("web_library_controller.hpp")
Q_MOC_INCLUDE("web_sources.hpp")
Q_MOC_INCLUDE("live_library_controller.hpp")
Q_MOC_INCLUDE("live_sources.hpp")
Q_MOC_INCLUDE("side_view.hpp")

class QTimer;

namespace seventhslide::db {
class BibleRepository;
class Database;
class FreeformRepository;
class MediaRepository;
class ServiceRepository;
class SongRepository;
}  // namespace seventhslide::db

namespace seventhslide::sync {
class Changed;
}

namespace seventhslide::freeform {
struct Document;
}

namespace seventhslide::render {
class OutputWindowHost;
}

namespace seventhslide::app {

class BackgroundThemeCopier;
class DestinationsController;
class SlideEditSession;
class FreeformLibraryController;
class LibraryController;
class LibraryRegistryController;
class LiveController;
class MediaController;
class MixerController;
class ServiceThumbs;
class ToolsController;
class FontController;
class ConvertController;
class DownloadController;
class ExportController;
class FreeformImportController;
class OutputListModel;
class OutputSettingsController;
class PlaybackController;
class PointerConfinement;
class ProfileController;
class ServiceItemModel;
class StackController;
class SideView;
class ThemeController;
class WebLibraryController;
class WebSources;
class LiveLibraryController;
class LiveSources;

class SessionFacade : public QObject,
                      public ILibrarySwitcher {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("main() creates the one session and hands it to the UI")

    Q_PROPERTY(qint64 openServiceId READ openServiceId NOTIFY sessionChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    /// Why the library database could not be opened, for the shell to show.
    /// Empty while the library is open.
    Q_PROPERTY(QString databaseError READ databaseError NOTIFY readyChanged)
    /// The open library is a viewer's (sync): nothing in it can be changed
    /// here, as nothing changed here would reach the other computers.
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY readOnlyChanged)
    /// The files what is live needs that are not here, as sync knows of them:
    /// ``MediaController::arrival`` with ``file``, its name, each. Empty when
    /// everything live is here.
    Q_PROPERTY(QVariantList liveArrivals READ liveArrivals NOTIFY liveArrivalsChanged)
    Q_PROPERTY(bool remoteEnabled READ remoteEnabled NOTIFY appSettingsChanged)
    Q_PROPERTY(int remotePort READ remotePort NOTIFY appSettingsChanged)
    /// The port the remote uses unless told otherwise.
    Q_PROPERTY(int defaultRemotePort READ defaultRemotePort CONSTANT)
    Q_PROPERTY(QString remotePin READ remotePin NOTIFY appSettingsChanged)
    /// How receivers on other computers take this one's NDI sources: "auto",
    /// "rudp", "udp", "multi_tcp" or "tcp" (single TCP).
    Q_PROPERTY(QString ndiTransport READ ndiTransport NOTIFY appSettingsChanged)
    /// True while ``ndiTransport`` waits for the app to start again: the NDI
    /// runtime, once loaded, keeps what it loaded with.
    Q_PROPERTY(bool ndiTransportPending READ ndiTransportPending NOTIFY appSettingsChanged)
    Q_PROPERTY(seventhslide::app::ServiceItemModel* serviceItems READ serviceItems CONSTANT)
    /// Every output, on or off (Settings, and anything that configures them).
    Q_PROPERTY(seventhslide::app::OutputListModel* outputs READ outputs CONSTANT)
    /// The outputs switched on: the operator's previews.
    Q_PROPERTY(seventhslide::app::OutputListModel* outputsOn READ outputsOn CONSTANT)
    /// What a well shows of the focused stack's Program, and moving within it
    /// (see side_view.hpp).
    Q_PROPERTY(seventhslide::app::SideView* program READ program CONSTANT)
    /// The same of its Preview, on a stack with one: what is cued.
    Q_PROPERTY(seventhslide::app::SideView* preview READ preview CONSTANT)
    /// Every output's theme library and its previews (see theme_controller.hpp).
    Q_PROPERTY(seventhslide::app::ThemeController* themes READ themes CONSTANT)
    /// Where each output paints: displays, NDI, cameras, Blackmagic
    /// (see destinations_controller.hpp).
    Q_PROPERTY(seventhslide::app::DestinationsController* destinations READ destinations CONSTANT)
    /// The slide editor: a Freeform item or a theme (see slide_edit_session.hpp).
    Q_PROPERTY(seventhslide::app::SlideEditSession* slideEdit READ slideEdit CONSTANT)
    /// The song, Bible and service library (see library_controller.hpp).
    Q_PROPERTY(seventhslide::app::LibraryController* library READ library CONSTANT)
    /// The image, video and audio library (see media_controller.hpp).
    Q_PROPERTY(seventhslide::app::MediaController* media READ media CONSTANT)
    /// The open library's fonts.
    Q_PROPERTY(seventhslide::app::FontController* fonts READ fonts CONSTANT)
    /// The Freeform document library (see freeform_library_controller.hpp).
    Q_PROPERTY(seventhslide::app::FreeformLibraryController* freeformLibrary READ freeformLibrary CONSTANT)
    /// The Browser tab's web pages (see web_library_controller.hpp).
    Q_PROPERTY(seventhslide::app::WebLibraryController* webLibrary READ webLibrary CONSTANT)
    /// The Live tab's sources (see live_library_controller.hpp).
    Q_PROPERTY(seventhslide::app::LiveLibraryController* liveLibrary READ liveLibrary CONSTANT)
    /// Stacks: the list, focus, send-to, blank / freeze, auto-advance
    /// (see stack_controller.hpp).
    Q_PROPERTY(seventhslide::app::StackController* stacks READ stacks CONSTANT)
    /// Saved style profiles of the open library (see profile_controller.hpp).
    Q_PROPERTY(seventhslide::app::ProfileController* profiles READ profiles CONSTANT)
    /// The registered libraries and which is open (see library_registry_controller.hpp).
    Q_PROPERTY(seventhslide::app::LibraryRegistryController* libraries READ libraries CONSTANT)
    /// Create, rename, resize, reorder, delete outputs
    /// (see output_settings_controller.hpp).
    Q_PROPERTY(seventhslide::app::OutputSettingsController* outputSettings READ outputSettings CONSTANT)
    /// What goes live, and the outputs' blank / freeze (see live_controller.hpp).
    Q_PROPERTY(seventhslide::app::LiveController* live READ live CONSTANT)
    /// The operator's output previews (see output_preview_controller.hpp).
    Q_PROPERTY(seventhslide::app::OutputPreviewController* outputPreviews READ outputPreviews CONSTANT)
    /// The Services window's thumbnails (see service_thumbs.hpp).
    Q_PROPERTY(seventhslide::app::ServiceThumbs* serviceThumbs READ serviceThumbs CONSTANT)
    /// The Audio Mixer: every stack's and output's level (see mixer_controller.hpp).
    Q_PROPERTY(seventhslide::app::MixerController* mixer READ mixer CONSTANT)
    /// ffmpeg, ffprobe and yt-dlp: where they are, and the jobs that run
    /// them (see tools_controller.hpp).
    Q_PROPERTY(seventhslide::app::ToolsController* tools READ tools CONSTANT)
    /// Convert: library files re-encoded into new ones (convert_controller.hpp).
    Q_PROPERTY(seventhslide::app::ConvertController* convert READ convert CONSTANT)
    /// Download from the internet with yt-dlp (download_controller.hpp).
    Q_PROPERTY(seventhslide::app::DownloadController* download READ download CONSTANT)
    /// Export: slides as images, songs as XML, media files back out
    /// (export_controller.hpp).
    Q_PROPERTY(seventhslide::app::ExportController* exporter READ exporter CONSTANT)
    Q_PROPERTY(seventhslide::app::FreeformImportController* freeformImport READ freeformImport CONSTANT)

public:
    explicit SessionFacade(QObject* parent = nullptr);
    ~SessionFacade() override;

    /// Open or create the library at ``path``, check its schema, load outputs and
    /// seed profiles. ``library_id`` is its entry in the registry; a file
    /// opened without one (tests) is never read-only, and its mixer is not
    /// kept.
    bool open_database(const std::filesystem::path& path, const std::string& library_id = {});
    /// In-memory DB. Tests only: the app never substitutes one for a library
    /// that failed to open (that would silently lose the session's work).
    bool open_memory_database();
    /// Why the library could not be opened; empty while it is open.
    [[nodiscard]] QString databaseError() const { return ready_ ? QString() : open_library_.last_error(); }
    /// Skip DB; install outputs directly (tests / headless). None is allowed.
    void set_outputs(std::vector<model::OutputConfig> outputs);

    void set_output_host(render::OutputWindowHost* host);
    /// How web pages are drawn (main() hands in Qt WebEngine's where it is
    /// built); without one a page live shows a black plate.
    void set_web_renderer_factory(WebRendererFactory factory);
    /// Open persisted display / NDI / camera destinations. Call after the QML
    /// engine has a window, so audience surfaces share a live graphics stack.
    void set_audience_outputs_enabled(bool on);
    /// Play the Audio Mixer's Listen mix on this computer's default sound
    /// device (falls back to a silent timer when none opens). Until this is
    /// called the mixer runs on the timer, so tests and headless runs make no
    /// sound. Call once the app's window is up.
    void start_listen_device();

    [[nodiscard]] bool has_build() const noexcept { return session_.has_build(); }

    /// Run ``fn`` for a second view of the session (the web remote) whose own
    /// stack tab is ``view_stack`` (empty: the console's). While it runs,
    /// commands that act on "the focused stack" act on that tab, and content
    /// it sends live moves that tab, not the console's. The console's focus is
    /// put back and its views are refreshed once ``fn`` returns.
    void run_in_view(std::string& view_stack, const std::function<void()>& fn);
    [[nodiscard]] qint64 openServiceId() const noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] bool readOnly() const noexcept { return open_library_.read_only(); }
    /// Make the open library one nothing is written to (a viewer's): the
    /// database refuses every write on this connection; sync still applies
    /// the others' changes on its own. Undone when another library opens.
    /// Made writable while open, it takes what opening it left unwritten.
    void set_read_only(bool read_only);
    /// Whether this computer only views library ``id``: sync's answer, from
    /// the library's member list. Asked as a library opens, before anything
    /// is written to it, and such a library opens read-only. An empty one
    /// says no.
    using ReadOnlyCheck = std::function<bool(const std::string& id)>;
    void set_read_only_check(ReadOnlyCheck check) { read_only_check_ = std::move(check); }
    /// Whether anything is live, on any stack.
    [[nodiscard]] bool anything_live() const;
    /// Sync wrote the other computers' changes into the ``changed`` records
    /// of the open library, on a connection of its own: read again what
    /// shows them, and nothing else (the songs by record, the other lists by
    /// table), and what is held here only to be written back (the outputs,
    /// the settings), so a later save cannot undo them. What is live stays
    /// as it is until the operator moves on.
    void reload_after_sync(const sync::Changed& changed);
    /// Library files ``paths`` ("<folder>/<name>" under the media folder)
    /// came from another computer, after what names them may have: whatever
    /// was shown or drawn without them is shown again with them, the outputs
    /// too.
    void media_arrived(const std::vector<std::string>& paths);
    [[nodiscard]] bool remoteEnabled() const noexcept
    {
        return open_library_.machine_settings().remote_enabled;
    }
    [[nodiscard]] int remotePort() const noexcept
    {
        return open_library_.machine_settings().remote_port;
    }
    [[nodiscard]] int defaultRemotePort() const noexcept { return model::k_default_remote_port; }
    /// ``port`` when the remote can use it, else the default (as saving does).
    Q_INVOKABLE int normalizeRemotePort(int port) const noexcept
    {
        return model::normalize_remote_port(port);
    }
    [[nodiscard]] QString remotePin() const
    {
        return QString::fromStdString(open_library_.machine_settings().remote_pin);
    }

    [[nodiscard]] ServiceItemModel* serviceItems() noexcept { return service_items_.get(); }
    [[nodiscard]] OutputListModel* outputs() noexcept { return outputs_model_.get(); }
    [[nodiscard]] OutputListModel* outputsOn() noexcept { return outputs_on_model_.get(); }
    [[nodiscard]] SideView* program() noexcept { return program_.get(); }
    [[nodiscard]] const SideView* program() const noexcept { return program_.get(); }
    [[nodiscard]] SideView* preview() noexcept { return preview_.get(); }
    [[nodiscard]] const SideView* preview() const noexcept { return preview_.get(); }
    [[nodiscard]] int preview_revision() const noexcept { return preview_revision_; }
    [[nodiscard]] QVariantList liveArrivals() const { return live_arrivals_; }
    /// First output on's name (controller / default preview target).
    [[nodiscard]] QString primary_output_name() const;

    [[nodiscard]] session::Session& session() noexcept { return session_; }
    [[nodiscard]] const session::Session& session() const noexcept { return session_; }
    /// The open library's database, settings and media folder (C++ callers).
    [[nodiscard]] OpenLibrary& open_library() noexcept { return open_library_; }
    [[nodiscard]] const OpenLibrary& open_library() const noexcept { return open_library_; }
    [[nodiscard]] db::SongRepository* song_repo() noexcept;
    [[nodiscard]] db::ServiceRepository* service_repo() noexcept;
    [[nodiscard]] db::BibleRepository* bible_repo() noexcept;
    [[nodiscard]] db::FreeformRepository* freeform_repo() noexcept;
    [[nodiscard]] db::MediaRepository* media_repo() noexcept;

    // --- Library panels ---
    void refresh_library();

    Q_INVOKABLE QVariantMap saveSongAsFreeform(qint64 song_id, const QString& name);
    /// Snapshot the current browse-selection passage as a Freeform item.
    Q_INVOKABLE QVariantMap saveBibleAsFreeform(int verse_start, int verse_end,
                                               const QString& name);
    [[nodiscard]] ThemeController* themes() noexcept { return themes_.get(); }
    [[nodiscard]] const ThemeController* themes() const noexcept { return themes_.get(); }
    [[nodiscard]] DestinationsController* destinations() noexcept { return destinations_.get(); }
    [[nodiscard]] SlideEditSession* slideEdit() noexcept { return slide_edit_.get(); }
    [[nodiscard]] const SlideEditSession* slideEdit() const noexcept { return slide_edit_.get(); }
    [[nodiscard]] LibraryController* library() noexcept { return library_.get(); }
    [[nodiscard]] const LibraryController* library() const noexcept { return library_.get(); }
    [[nodiscard]] MediaController* media() noexcept { return media_.get(); }
    [[nodiscard]] FreeformLibraryController* freeformLibrary() noexcept
    {
        return freeform_library_.get();
    }
    [[nodiscard]] WebLibraryController* webLibrary() noexcept { return web_library_.get(); }
    /// The web pages live, each drawn by its renderer (web_sources.hpp).
    [[nodiscard]] WebSources* web_sources() noexcept { return web_sources_.get(); }
    [[nodiscard]] LiveLibraryController* liveLibrary() noexcept { return live_library_.get(); }
    /// The live sources live, each received by its receiver (live_sources.hpp).
    [[nodiscard]] LiveSources* live_sources() noexcept { return live_sources_.get(); }
    /// How live sources are received (NDI's by default; the tests their own).
    void set_live_receiver_factory(LiveReceiverFactory factory);
    /// What holds the pointer on a web page worked from the Controller
    /// (main() hands in the windowing system's own; the tests theirs);
    /// without one, it is not held.
    void set_pointer_confinement(std::unique_ptr<PointerConfinement> confinement);
    [[nodiscard]] PointerConfinement* pointer_confinement() noexcept
    {
        return pointer_confinement_.get();
    }
    [[nodiscard]] StackController* stacks() noexcept { return stack_ctl_.get(); }
    [[nodiscard]] ProfileController* profiles() noexcept { return profile_ctl_.get(); }
    [[nodiscard]] LibraryRegistryController* libraries() noexcept { return library_reg_.get(); }
    [[nodiscard]] OutputSettingsController* outputSettings() noexcept { return output_settings_.get(); }
    [[nodiscard]] PlaybackController* playback() noexcept { return playback_.get(); }
    [[nodiscard]] LiveController* live() noexcept { return live_.get(); }
    [[nodiscard]] const LiveController* live() const noexcept { return live_.get(); }
    [[nodiscard]] OutputPreviewController* outputPreviews() noexcept { return output_previews_.get(); }
    [[nodiscard]] ServiceThumbs* serviceThumbs() noexcept { return service_thumbs_.get(); }
    [[nodiscard]] MixerController* mixer() noexcept { return mixer_.get(); }
    [[nodiscard]] ToolsController* tools() noexcept { return tools_.get(); }
    [[nodiscard]] FontController* fonts() noexcept { return fonts_.get(); }
    [[nodiscard]] ConvertController* convert() noexcept { return convert_.get(); }
    /// The fonts and measurer every preview draws with (OverlayTextItem).
    [[nodiscard]] PreviewResources& previews() noexcept { return previews_; }
    [[nodiscard]] DownloadController* download() noexcept { return download_.get(); }
    [[nodiscard]] ExportController* exporter() noexcept { return exporter_.get(); }
    [[nodiscard]] FreeformImportController* freeformImport() noexcept { return freeform_import_.get(); }

    /// Where the open library keeps its media and theme assets. Opening a
    /// library sets it; the output windows and the preview cache follow.
    void set_media_root(std::filesystem::path root);
    [[nodiscard]] const std::filesystem::path& media_root() const noexcept
    {
        return open_library_.media_root();
    }

    // --- App settings ---
    /// CCLI, theme priority, preview video, font bundling, remote prefs.
    Q_INVOKABLE QVariantMap getAppSettings();
    /// Persist settings map. Empty return = ok.
    Q_INVOKABLE QString updateAppSettings(const QVariantMap& fields);
    [[nodiscard]] QString ndiTransport() const;
    [[nodiscard]] bool ndiTransportPending() const;
    /// Keep ``transport`` (an ``ndiTransport`` key) as this computer's NDI
    /// transport.
    /// Empty return = ok.
    Q_INVOKABLE QString setNdiTransport(const QString& transport);

    // --- Libraries: whole content collections (own database + media tree) ---
    /// Load the registry rooted at ``data_dir`` and this computer's
    /// settings, opening no library yet.
    void load_libraries(const std::filesystem::path& data_dir);
    /// Open the registry's active library. False when it will not open
    /// (``databaseError`` says why).
    bool open_active_library();
    /// ``load_libraries``, then ``open_active_library``, with nothing between:
    /// for the tests that need no sync. The app makes its sync between the
    /// two (main.cpp), so a library it only views opens read-only.
    bool init_libraries(std::filesystem::path data_dir);

    // --- System ---
    /// Candidate LAN URLs for the web remote. Empty if none.
    QStringList list_remote_candidate_urls(int port = model::k_default_remote_port) const;

signals:
    /// Live content or session state changed. Every mutation emits it; the
    /// narrower signals below also fire alone for focus, playback, frames and stacks.
    void sessionChanged();
    /// The focused stack changed, or its content did. Properties describing the
    /// controller's view (mode, line cursor, gallery kind) notify on this.
    void focusedStackChanged();
    /// A take traded the focused stack's Program and Preview (on this
    /// computer or from the web remote). Comes before the sides' lines are
    /// reloaded and before the focusedStackChanged that shows them traded.
    /// The sides already answer for what they now hold; only the views on
    /// them (where their lists stand) are still as they were. The
    /// Controller's wells trade those, so each lands where the other was.
    void focusedSidesSwapping();
    /// Video or audio position / duration / play state. Fires on every
    /// playback tick (about 30 Hz while anything plays), on focus switches,
    /// and after any mutation (it follows ``sessionChanged``). Kept apart from
    /// ``sessionChanged`` so playback does not re-run every binding.
    void mediaStateChanged();
    /// A new content or background video frame was published (every video tick).
    /// Only the frame images changed: the scene frames did not.
    void videoFrameChanged();
    /// A web page live drew a new picture (LiveWebItem follows it).
    void webFrameChanged();
    /// A live source sent a new picture (LiveSourceItem follows it).
    void liveSourceFrameChanged();
    /// Emitted on every present pass (mutations, clock ticks, seeks), once the
    /// destinations - the output cards' previews among them - have the frames.
    void framesChanged();
    /// A newer preview image is available under the same provider URLs.
    void previewRevisionChanged();
    void liveArrivalsChanged();
    void readyChanged();
    void readOnlyChanged();
    void appSettingsChanged();
    /// An entry point failed and has no return value to carry the reason (its
    /// own, or one of its controllers'). The shell shows ``message`` to the
    /// operator; see entry_error.hpp.
    void errorOccurred(const QString& message);

private:
    /// Connect what each controller announces to what follows from it.
    void wire_controllers();
    /// Connect what a well's side follows (focus, media, the clock, pages,
    /// sources) and what it announces.
    void wire_side(SideView& side);
    /// Each side a well shows: the Program's, then the Preview's.
    [[nodiscard]] std::array<SideView*, 2> sides() noexcept
    {
        return {program_.get(), preview_.get()};
    }
    /// Remove the staging files an interrupted copy left in the media root.
    void sweep_import_staging() const;
    /// Save just-converted ``doc`` as a new item once its slides own their
    /// background files. The result's ``id`` is -1 with no
    /// ``error`` while a file is still being copied in; the item is saved
    /// when it is, and anything that went wrong goes to ``errorOccurred``.
    QVariantMap save_converted(freeform::Document doc);
    /// Why ``doc``, laid out just now, cannot be saved yet: a font it names is
    /// on its way, and its words were measured in the stand-in. Empty when it
    /// can.
    [[nodiscard]] QString why_fonts_still_coming(const freeform::Document& doc) const;
    QVariantMap save_bible_ref_as_freeform(qint64 bible_id,
                                           const QString& book_name,
                                           int chapter,
                                           int verse_start,
                                           int verse_end,
                                           const QString& name);
    /// Open ``db``, library ``library_id`` (``open_database``).
    bool finish_open_database(db::Database db, const std::string& library_id);
    /// Record why a library failed to open and drop to not-ready, letting go
    /// of whatever of it was attached; returns false.
    bool fail_open_database(const std::exception& ex);
    /// The controllers let go of the open database, it closes, nothing
    /// resolves files against its media folder any more, and nothing of it is
    /// still listed or cached for previews. Not ready after; the caller says
    /// so (``readyChanged``).
    void release_library();
    // --- ILibrarySwitcher ---
    /// Tear down every per-library object so another database can be opened.
    void close_library() override;
    /// Point media/theme roots at ``entry`` and open its database.
    [[nodiscard]] bool open_library(const library::LibraryEntry& entry) override;
    /// Re-read everything a profile switch just rewrote and repaint.
    void apply_profile_switch();
    /// The open library's outputs, repaired by ``prepare_outputs`` and written
    /// back when that changed anything (a read-only library is repaired here
    /// only). A library ``made_now`` (its database created by this open)
    /// starts with Main; any other may have none.
    [[nodiscard]] std::vector<model::OutputConfig> load_library_outputs(bool made_now);
    /// Write what opening the library put right while it was read-only: the
    /// outputs and a profile made active. Failures go to ``errorOccurred``.
    void write_open_repairs();
    /// Bring ``outputs`` to what the app relies on: a stable unique id on
    /// each, and Base themes + category defaults. Returns true when anything
    /// changed.
    static bool prepare_outputs(std::vector<model::OutputConfig>& outputs);
    /// The output a library made from nothing starts with.
    [[nodiscard]] static model::OutputConfig make_default_main();
    /// Save the session's outputs to the open library (the controllers' save
    /// hook); throws when that fails.
    void save_outputs();
    /// The outputs were edited in place and saved: rebuild what is live.
    void rebuild_after_outputs_edited();
    /// Switch off, in the session, the outputs this computer keeps off for
    /// the open library.
    void load_outputs_off();
    /// An output was switched on or off: close or open what it paints to,
    /// and refresh what lists the outputs on.
    void apply_outputs_on();

    /// Live content or the rundown changed: reload the models that follow
    /// it, refresh the stack controller, and queue a repaint. Every
    /// controller's liveChanged lands here.
    void after_mutation(bool rebuild_models);
    /// End a ``run_in_view``: hand the console its focus back, then announce.
    void leave_view();
    void queue_present_frames();
    void bump_preview_revision();
    void sync_output_host_state();
    /// Keep ``host`` (null for none) here and in every controller that paints
    /// to it, so none is left holding one that has gone.
    void hand_out_output_host(render::OutputWindowHost* host);
    /// Tell the output host which outputs are off.
    void push_outputs_off();
    /// Give the output host every output's display name, by id.
    void push_output_names();
    /// Compose every output and publish the frames.
    void present_frames();
    /// Hand ``presented_frames_`` to the destinations (the operator's
    /// previews draw from theirs), then run the clock tick while any output
    /// shows a clock.
    /// ``every_output``: the pass composed every output, so what any output
    /// showed before it is gone; otherwise it composed only some.
    void publish_frames(bool every_output);
    /// Warm the image slides either side of the live one, so moving to them
    /// does not hitch on a full decode.
    void prefetch_neighbour_images();
    /// The library files what is live paints or plays, by path under the media
    /// folder ("images/<name>"): what every output shows now, every picture of
    /// an image folder, the video or sound, and every slide of a Freeform item.
    [[nodiscard]] std::set<std::string> live_media() const;
    /// Work out ``liveArrivals`` again, telling only if it changed.
    void follow_live_arrivals();

    void restore_output_windows();
    /// Hand the output host the open library's destinations. Does nothing
    /// without a library registry (an in-memory database in tests).
    void load_output_destinations();
    /// Store the host's destinations under the library they were loaded for.
    void save_output_destinations();
    void load_app_settings();
    void apply_app_settings_live(bool rebuild);
    void on_clock_tick();

    /// Before the session, which measures with ``previews_``; the controllers
    /// below borrow both.
    PreviewResources previews_;
    OpenLibrary open_library_;
    session::Session session_;
    /// The Audio Mixer and its engine. Before the other controllers: the
    /// playback controller's audio players are the engine's sources, and go
    /// first.
    std::unique_ptr<MixerController> mixer_;
    std::unique_ptr<ToolsController> tools_;
    std::unique_ptr<FontController> fonts_;

    std::unique_ptr<ServiceItemModel> service_items_;
    std::unique_ptr<OutputListModel> outputs_model_;
    std::unique_ptr<OutputListModel> outputs_on_model_;
    std::unique_ptr<SideView> program_;
    std::unique_ptr<SideView> preview_;
    std::unique_ptr<ThemeController> themes_;
    std::unique_ptr<DestinationsController> destinations_;
    std::unique_ptr<LibraryController> library_;
    std::unique_ptr<MediaController> media_;
    /// Copies background themes into Freeform slides.
    std::unique_ptr<BackgroundThemeCopier> bg_copier_;
    std::unique_ptr<FreeformLibraryController> freeform_library_;
    std::unique_ptr<WebLibraryController> web_library_;
    std::unique_ptr<WebSources> web_sources_;
    std::unique_ptr<LiveLibraryController> live_library_;
    std::unique_ptr<LiveSources> live_sources_;
    std::unique_ptr<PointerConfinement> pointer_confinement_;
    /// After the Freeform library, which stores the documents it edits.
    std::unique_ptr<SlideEditSession> slide_edit_;
    std::unique_ptr<StackController> stack_ctl_;
    std::unique_ptr<ProfileController> profile_ctl_;
    std::unique_ptr<LibraryRegistryController> library_reg_;
    std::unique_ptr<OutputSettingsController> output_settings_;
    std::unique_ptr<PlaybackController> playback_;
    std::unique_ptr<LiveController> live_;
    std::unique_ptr<OutputPreviewController> output_previews_;
    std::unique_ptr<ServiceThumbs> service_thumbs_;
    /// After the tools and the media library it converts into.
    std::unique_ptr<ConvertController> convert_;
    std::unique_ptr<DownloadController> download_;
    /// Last: it reads from the library controllers above, and its worker
    /// stops before any of them goes.
    std::unique_ptr<ExportController> exporter_;
    std::unique_ptr<FreeformImportController> freeform_import_;

    render::OutputWindowHost* host_ = nullptr;
    bool audience_outputs_enabled_ = false;
    QTimer* clock_tick_ = nullptr;
    bool present_queued_ = false;
    /// Frames composed by the present pass in flight, for the destinations.
    std::vector<scene::SharedSceneFrame> presented_frames_;
    /// What each output shows, by output id: the frames of the last full
    /// pass, and the clock ticks' since.
    std::map<std::string, scene::SharedSceneFrame, std::less<>> shown_frames_;
    int preview_revision_ = 0;
    QVariantList live_arrivals_;
    bool ready_ = false;
    ReadOnlyCheck read_only_check_;
    /// The session's outputs were put right and not written: the library
    /// is read-only.
    bool outputs_unwritten_ = false;

    /// Set while ``run_in_view`` runs a command for another view.
    struct ViewScope {
        std::string* view_stack = nullptr;
        /// The focus the command started on (the view's tab).
        std::string entered;
        /// The console's own focus, put back afterwards.
        std::string console;
        /// The console's tab had something live or cued when the view entered.
        bool console_held = false;
        /// after_mutation calls held back until the console has its focus.
        bool pending = false;
        bool rebuild = false;
    };
    std::optional<ViewScope> view_scope_;
    /// The stacks a take traded the sides of (once per take) since
    /// after_mutation last announced; it says so for the focused one's.
    std::vector<std::string> swapped_stacks_;

};

} // namespace seventhslide::app
