// SessionFacade: construction, the open library, and application settings.
//
// The class is declared in session_facade.hpp and implemented across several
// translation units, one per domain (see the others beside this file). This
// one holds the object's own lifetime and wiring, opening and switching
// libraries, style profiles, and app settings.

#include "session_facade.hpp"

#include "background_theme_copier.hpp"

#include "font_controller.hpp"

#include "destinations_controller.hpp"
#include "entry_error.hpp"
#include "refusals.hpp"
#include "slide_edit_session.hpp"
#include "freeform_library_controller.hpp"
#include "library_controller.hpp"
#include "lan_addresses.hpp"
#include "library_registry_controller.hpp"
#include "live_controller.hpp"
#include "media_controller.hpp"
#include "mixer_controller.hpp"
#include "tools_controller.hpp"
#include "convert_controller.hpp"
#include "download_controller.hpp"
#include "export_controller.hpp"
#include "freeform_import_controller.hpp"
#include "media_thumb_cache.hpp"
#include "theme_thumb_cache.hpp"
#include "output_list_model.hpp"
#include "output_settings_controller.hpp"
#include "playback_controller.hpp"
#include "pointer_confinement.hpp"
#include "profile_controller.hpp"
#include "qvariant_json.hpp"
#include "service_item_model.hpp"
#include "service_thumbs.hpp"
#include "stack_controller.hpp"
#include "side_view.hpp"
#include "theme_controller.hpp"
#include "web_library_controller.hpp"
#include "web_sources.hpp"
#include "live_library_controller.hpp"
#include "live_sources.hpp"


#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/db/schema.hpp>
#include <seventhslide/db/media_kind.hpp>
#include <seventhslide/platform/paths.hpp>
#include <seventhslide/platform/tools.hpp>
#include <seventhslide/freeform/doc_edit.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/model/stack.hpp>
#include <seventhslide/model/static_media.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/sync/changed.hpp>
#include <seventhslide/theme/theme_library.hpp>
#include <seventhslide/util/atomic_file.hpp>

#include <QCoreApplication>
#include <QThreadPool>
#include <QTimer>

#include <optional>
#include <type_traits>
#include <string>
#include <algorithm>
#include <string_view>
#include <initializer_list>
#include <map>
#include <vector>
#include <utility>

namespace seventhslide::app {

SessionFacade::SessionFacade(QObject* parent)
    : QObject(parent)
    , session_(previews_.measurer())
    , mixer_(std::make_unique<MixerController>(session_, open_library_, this))
    , tools_(std::make_unique<ToolsController>(
          platform::ToolSearch::from(platform::Paths{},
                                     QCoreApplication::applicationDirPath().toStdString()),
          this))
    , fonts_(std::make_unique<FontController>(open_library_, previews_, this))
    , service_items_(std::make_unique<ServiceItemModel>(session_, this))
    , outputs_model_(std::make_unique<OutputListModel>(session_, false, this))
    , outputs_on_model_(std::make_unique<OutputListModel>(session_, true, this))
    , program_(std::make_unique<SideView>(session_, session::StackSide::Program, this))
    , preview_(std::make_unique<SideView>(session_, session::StackSide::Preview, this))
    , themes_(std::make_unique<ThemeController>(session_, open_library_, previews_, this))
    , destinations_(std::make_unique<DestinationsController>(session_, this))
    , library_(std::make_unique<LibraryController>(session_, this))
    , media_(std::make_unique<MediaController>(open_library_, session_, *library_, this))
    , freeform_library_(std::make_unique<FreeformLibraryController>(open_library_, session_,
                                                                    previews_, *library_, this))
    , web_library_(std::make_unique<WebLibraryController>(session_, *library_, open_library_, this))
    , web_sources_(std::make_unique<WebSources>(session_, &mixer_->engine().mixer(), this))
    , live_library_(std::make_unique<LiveLibraryController>(session_, *library_, open_library_, this))
    , live_sources_(std::make_unique<LiveSources>(session_, &mixer_->engine().mixer(), this))
    , slide_edit_(std::make_unique<SlideEditSession>(session_, previews_, *freeform_library_,
                                                     *themes_, this))
    , stack_ctl_(std::make_unique<StackController>(session_, open_library_, this))
    , profile_ctl_(std::make_unique<ProfileController>(open_library_, this))
    , library_reg_(std::make_unique<LibraryRegistryController>(open_library_, *this, this))
    , output_settings_(std::make_unique<OutputSettingsController>(session_, open_library_, this))
    , playback_(std::make_unique<PlaybackController>(session_, open_library_,
                                                     mixer_->engine().mixer(),
                                                     this))
    , live_(std::make_unique<LiveController>(session_, *library_, *freeform_library_, *media_,
                                             this))
    , output_previews_(std::make_unique<OutputPreviewController>(session_, *playback_,
                                                                 open_library_, previews_,
                                                                 this))
    , service_thumbs_(std::make_unique<ServiceThumbs>(session_, *library_, previews_, this))
    , convert_(std::make_unique<ConvertController>(*tools_, open_library_, *media_, this))
    , download_(std::make_unique<DownloadController>(*tools_, open_library_, *media_, this))
    , exporter_(std::make_unique<ExportController>(session_, previews_, open_library_, *library_,
                                                   *freeform_library_, *media_, this))
    , freeform_import_(std::make_unique<FreeformImportController>(session_, open_library_, *freeform_library_,
                                                                  *media_, *fonts_, *library_, this))
{
    bg_copier_ = std::make_unique<BackgroundThemeCopier>(session_, open_library_, *media_, this);
    slide_edit_->set_background_copier(bg_copier_.get());
    live_sources_->set_factory(live_receiver_factory());
    live_library_->set_receiver_factory(live_receiver_factory());
    live_library_->set_live_sources(live_sources_.get());
    clock_tick_ = new QTimer(this);
    clock_tick_->setInterval(1000);
    connect(clock_tick_, &QTimer::timeout, this, [this]() { on_clock_tick(); });

    mixer_->sync();
    wire_controllers();
}

SessionFacade::~SessionFacade()
{
    // A copy still coming in has nowhere to land.
    slide_edit_->set_background_copier(nullptr);
    // The host may outlive this (main() makes it first): its sinks leave
    // the mixer before the mixer goes.
    if (host_ != nullptr) {
        host_->set_audio_mixer(nullptr);
    }
    // The clock stops before the players it reads are taken down.
    mixer_->engine().stop();
}

void SessionFacade::start_listen_device()
{
    mixer_->start_listen_device();
}

db::SongRepository* SessionFacade::song_repo() noexcept
{
    return library_->song_repo();
}

db::ServiceRepository* SessionFacade::service_repo() noexcept
{
    return library_->service_repo();
}

db::BibleRepository* SessionFacade::bible_repo() noexcept
{
    return library_->bible_repo();
}

db::FreeformRepository* SessionFacade::freeform_repo() noexcept
{
    return freeform_library_->repo();
}

db::MediaRepository* SessionFacade::media_repo() noexcept
{
    return media_->media_repo();
}

model::OutputConfig SessionFacade::make_default_main()
{
    return theme::new_output("Main", {});
}

void SessionFacade::wire_side(SideView& side)
{
    connect(&side, &SideView::liveChanged, this, &SessionFacade::after_mutation);
    connect(&side, &SideView::errorOccurred, this, &SessionFacade::errorOccurred);
    // A scrub draws the picture held there again; nothing went live.
    connect(&side, &SideView::sought, playback_.get(), &PlaybackController::follow_seek);
    connect(this, &SessionFacade::focusedStackChanged, &side, &SideView::focusedStackChanged);
    connect(this, &SessionFacade::mediaStateChanged, &side, &SideView::mediaStateChanged);
    connect(playback_.get(), &PlaybackController::mediaPositionChanged, &side,
            &SideView::mediaPositionChanged);
    connect(this, &SessionFacade::focusedStackChanged, &side, &SideView::liveSourceStateChanged);
    connect(this, &SessionFacade::focusedStackChanged, &side, &SideView::webStateChanged);
    side.set_web_sources(web_sources_.get());
    side.set_live_sources(live_sources_.get());
}

// The controllers know nothing of each other or of this object: each says
// what changed, and this is the one place that decides what follows.
void SessionFacade::wire_controllers()
{
    // Live content (or the rundown) moved: reload what follows it and repaint.
    const auto follow_live = [this](auto& controller) {
        using Controller = std::remove_reference_t<decltype(controller)>;
        connect(&controller, &Controller::liveChanged, this, &SessionFacade::after_mutation);
    };
    follow_live(*library_);
    follow_live(*media_);
    follow_live(*freeform_library_);
    follow_live(*web_library_);
    follow_live(*live_library_);
    follow_live(*stack_ctl_);
    follow_live(*live_);
    // A take traded a stack's sides; after_mutation says so for the focused
    // stack's, once the console has its focus (see focusedSidesSwapping).
    connect(stack_ctl_.get(), &StackController::sidesSwapped, this,
            [this](const QString& stack_id) { swapped_stacks_.push_back(stack_id.toStdString()); });
    connect(playback_.get(), &PlaybackController::previewChanged, this,
            &SessionFacade::bump_preview_revision);
    connect(playback_.get(), &PlaybackController::repaintRequested, this,
            &SessionFacade::present_frames);
    connect(profile_ctl_.get(), &ProfileController::profileSwitched, this,
            &SessionFacade::apply_profile_switch);

    // The outputs were edited in place (a theme, a name, the order): the
    // controllers save them through this, and once saved, what is live is
    // rebuilt. Switching one on or off is this computer's alone, and the
    // session has rebuilt for it. Destinations change only how they are shown.
    themes_->set_save_outputs([this] { save_outputs(); });
    output_settings_->set_save_outputs([this] { save_outputs(); });
    connect(themes_.get(), &ThemeController::outputsEdited, this,
            &SessionFacade::rebuild_after_outputs_edited);
    connect(output_settings_.get(), &OutputSettingsController::outputsEdited, this,
            &SessionFacade::rebuild_after_outputs_edited);
    connect(output_settings_.get(), &OutputSettingsController::outputsSwitched, this,
            &SessionFacade::apply_outputs_on);
    connect(destinations_.get(), &DestinationsController::outputStateChanged, this,
            &SessionFacade::sync_output_host_state);
    connect(destinations_.get(), &DestinationsController::repaintRequested, this,
            &SessionFacade::present_frames);
    connect(destinations_.get(), &DestinationsController::restoreRequested, this,
            &SessionFacade::restore_output_windows);
    connect(destinations_.get(), &DestinationsController::liveStateChanged, this,
            &SessionFacade::sessionChanged);

    // One place for the shell to hear about failures that have no return value.
    const auto report_errors = [this](auto& controller) {
        using Controller = std::remove_reference_t<decltype(controller)>;
        connect(&controller, &Controller::errorOccurred, this, &SessionFacade::errorOccurred);
    };
    report_errors(*library_);
    report_errors(*media_);
    report_errors(*freeform_library_);
    report_errors(*web_library_);
    report_errors(*live_library_);
    report_errors(*slide_edit_);
    themes_->add_designer(slide_edit_.get());
    report_errors(*themes_);
    report_errors(*stack_ctl_);
    report_errors(*library_reg_);
    report_errors(*output_settings_);
    report_errors(*live_);
    report_errors(*mixer_);
    report_errors(*fonts_);

    // The facade's own properties (remote settings) follow these; the
    // controllers' properties notify on their own signals.
    connect(this, &SessionFacade::sessionChanged, this, &SessionFacade::mediaStateChanged);
    // Before anything reads the media state on a side, so the playback's
    // ticks compare with no later a reading than the seek bar's.
    connect(this, &SessionFacade::mediaStateChanged, playback_.get(),
            [this] { playback_->media_state_announced(); });
    connect(stack_ctl_.get(), &StackController::appSettingsChanged, this,
            &SessionFacade::appSettingsChanged);
    connect(stack_ctl_.get(), &StackController::mediaStateChanged, this,
            &SessionFacade::mediaStateChanged);
    connect(playback_.get(), &PlaybackController::mediaStateChanged, this,
            &SessionFacade::mediaStateChanged);
    connect(playback_.get(), &PlaybackController::videoFrameChanged, this,
            &SessionFacade::videoFrameChanged);
    output_previews_->set_web_sources(web_sources_.get());
    connect(web_sources_.get(), &WebSources::framesChanged, this, &SessionFacade::webFrameChanged);
    output_previews_->set_live_sources(live_sources_.get());
    connect(live_sources_.get(), &LiveSources::framesChanged, this,
            &SessionFacade::liveSourceFrameChanged);
    for (auto* side : sides()) {
        wire_side(*side);
    }
    // Focus moved without anything going live: the line map and the live
    // rundown row show the focused stack, so they follow before anything
    // reads them.
    connect(stack_ctl_.get(), &StackController::focusedStackChanged, this, [this]() {
        for (auto* side : sides()) {
            side->reload_lines();
        }
        service_items_->notify_selection_changed();
        emit focusedStackChanged();
    });

    // The Freeform editor's slide backdrops are theme renders, and so are the
    // Freeform library's tiles, whose URLs name the themes and canvases. A
    // theme edit's outputsEdited comes with its libraryChanged, so the tiles
    // follow that alone.
    connect(themes_.get(), &ThemeController::libraryChanged, slide_edit_.get(),
            &SlideEditSession::on_theme_library_changed);
    connect(themes_.get(), &ThemeController::libraryChanged, freeform_library_.get(),
            &FreeformLibraryController::rebuild_freeform_browser);
    connect(output_settings_.get(), &OutputSettingsController::outputsEdited, freeform_library_.get(),
            &FreeformLibraryController::rebuild_freeform_browser);
    // Opening another library replaces its profiles, and the media caches
    // let go of the outgoing library's files.
    connect(library_reg_.get(), &LibraryRegistryController::profilesChanged, profile_ctl_.get(),
            &ProfileController::profilesChanged);
    // What is live says what of it is still on its way, as that changes.
    connect(media_.get(), &MediaController::arrivalsChanged, this, &SessionFacade::follow_live_arrivals);
    connect(library_reg_.get(), &LibraryRegistryController::librarySwitching, media_.get(),
            &MediaController::on_library_switched);
    // A font changing under text moves it, so that waits until nothing is live.
    fonts_->set_may_write([this] { return !open_library_.read_only(); });
    library_->set_may_write([this] { return !open_library_.read_only(); });
    fonts_->set_may_reload([this] { return !anything_live(); });
    // A theme sharing a background still on its way waits for it, rather
    // than share it for good; removing a font still coming says so.
    themes_->set_on_its_way([this](const std::string& path) { return media_->on_its_way(path); });
    fonts_->set_on_its_way([this](const std::string& path) { return media_->on_its_way(path); });
    connect(this, &SessionFacade::sessionChanged, fonts_.get(), &FontController::retry_waiting);
    // The themes' revision and the Freeform tiles follow library_changed.
    connect(fonts_.get(), &FontController::fontsReloaded, this, [this] {
        session_.rebuild();
        themes_->library_changed();
        after_mutation(true);
    });
}

void SessionFacade::set_audience_outputs_enabled(bool on)
{
    if (audience_outputs_enabled_ == on) {
        return;
    }
    audience_outputs_enabled_ = on;
    if (on) {
        restore_output_windows();
    }
}

void SessionFacade::set_web_renderer_factory(WebRendererFactory factory)
{
    web_library_->set_renderer_factory(factory);
    web_sources_->set_factory(std::move(factory));
    web_sources_->sync();
}

void SessionFacade::set_live_receiver_factory(LiveReceiverFactory factory)
{
    live_library_->set_receiver_factory(factory);
    live_sources_->set_factory(std::move(factory));
    live_sources_->sync();
}

void SessionFacade::set_pointer_confinement(std::unique_ptr<PointerConfinement> confinement)
{
    pointer_confinement_ = std::move(confinement);
}

void SessionFacade::set_output_host(render::OutputWindowHost* host)
{
    if (host_ == host) {
        return;
    }
    if (host_ != nullptr) {
        QObject::disconnect(host_, nullptr, this, nullptr);
        host_->set_audio_mixer(nullptr);
    }
    hand_out_output_host(host);
    if (host_ != nullptr) {
        // A host may go first (tests make it after the session): its sinks
        // leave the mixer as it does, and nothing here or in the controllers
        // reaches for it after.
        QObject::connect(host_, &QObject::destroyed, this,
                         [this]() { hand_out_output_host(nullptr); });
        QObject::connect(host_,
                         &render::OutputWindowHost::changed,
                         this,
                         [this]() {
                             destinations_->notify_changed();
                             sync_output_host_state();
                         });
        QObject::connect(host_, &render::OutputWindowHost::destinationsEdited, this,
                         [this]() { save_output_destinations(); });
        QObject::connect(host_, &render::OutputWindowHost::sinkStatusChanged,
                         destinations_.get(), &DestinationsController::statusChanged);
        // The destinations send their output's mix.
        host_->set_audio_mixer(&mixer_->engine().mixer());
        host_->set_font_faces(&previews_.measurer().faces());
        if (!open_library_.media_root().empty()) {
            host_->set_static_root(open_library_.media_root());
        }
        destinations_->refreshDisplays();
        if (ready_) {
            load_output_destinations();
        }
        if (ready_ && audience_outputs_enabled_) {
            restore_output_windows();
        } else {
            sync_output_host_state();
        }
    }
}

void SessionFacade::hand_out_output_host(render::OutputWindowHost* host)
{
    host_ = host;
    destinations_->set_output_host(host);
    output_settings_->set_output_host(host);
    playback_->set_output_host(host);
    output_previews_->set_output_host(host);
    web_sources_->set_output_host(host);
    live_sources_->set_output_host(host);
    if (host_ == nullptr) {
        for (auto* model : {outputs_model_.get(), outputs_on_model_.get()}) {
            model->clear_host_state();
            model->notify_rows_changed();
        }
    }
}

bool SessionFacade::open_database(const std::filesystem::path& path, const std::string& library_id)
{
    try {
        auto db = db::Database::open_file(path);
        // The media root is live before the database is: finish_open_database
        // rebuilds the browsers, which resolve thumbnails against it. One
        // spelling of the library layout - see seventhslide::library.
        set_media_root(library::media_root(path.parent_path()));
        sweep_import_staging();
        return finish_open_database(std::move(db), library_id);
    } catch (const std::exception& ex) {
        return fail_open_database(ex);
    }
}

bool SessionFacade::open_memory_database()
{
    try {
        auto db = db::Database::open_memory();
        return finish_open_database(std::move(db), {});
    } catch (const std::exception& ex) {
        return fail_open_database(ex);
    }
}

bool SessionFacade::fail_open_database(const std::exception& ex)
{
    // The schema check may have passed, the repositories attached and the
    // lists filled before the failure; and the media folder is set before the
    // database opens.
    release_library();
    open_library_.set_last_error(report_entry_error("open library", ex));
    emit readyChanged();
    return false;
}

bool SessionFacade::finish_open_database(db::Database db, const std::string& library_id)
{
    bool made_now = false;
    try {
        made_now = db::ensure_schema(db);
    } catch (const std::exception& ex) {
        return fail_open_database(ex);
    }
    open_library_.set_last_error({});

    auto& database = open_library_.open(std::move(db));
    // The outputs this computer keeps for it are kept by the library opening,
    // not the active one, which a switch may be changing. One opened without
    // its entry (tests) is taken as the active one.
    const auto* registry = open_library_.registry();
    open_library_.set_destinations_library_id(
        !library_id.empty() || registry == nullptr ? library_id : registry->active_id());
    // Before anything is written to it: a library this computer only views
    // takes no write, the repairs below included.
    if (!library_id.empty() && read_only_check_ && read_only_check_(library_id)) {
        set_read_only(true);
    }
    fonts_->library_opened();
    library_->attach(database);
    media_->attach(database);
    freeform_library_->attach(database);
    web_library_->attach(database);

    // Outputs first: the profile service keys its first snapshot by output id.
    session_.set_outputs(load_library_outputs(made_now));
    load_outputs_off();
    open_library_.start_profiles(*song_repo(), session_.outputs());

    ready_ = true;
    load_app_settings();
    library_->refresh_services();
    refresh_library();
    // The stacks and outputs are the library's now. A database not in the
    // registry (tests) has no id, and its mixer is not kept.
    mixer_->open_library(library_id);
    after_mutation(true);
    load_output_destinations();
    restore_output_windows();
    emit readyChanged();
    return true;
}

namespace {

[[nodiscard]] render::NdiTransport render_transport(model::NdiTransport transport)
{
    switch (transport) {
    case model::NdiTransport::ReliableUdp:
        return render::NdiTransport::ReliableUdp;
    case model::NdiTransport::Udp:
        return render::NdiTransport::Udp;
    case model::NdiTransport::MultiTcp:
        return render::NdiTransport::MultiTcp;
    case model::NdiTransport::SingleTcp:
        return render::NdiTransport::SingleTcp;
    case model::NdiTransport::Automatic:
        break;
    }
    return render::NdiTransport::Automatic;
}

/// Hand the NDI runtime this computer's transport before anything loads it.
void apply_ndi_transport(const OpenLibrary& library)
{
    const auto* store = library.machine_store();
    if (store == nullptr) {
        return;
    }
    render::ndi_set_transport(
        render_transport(library.machine_settings().ndi_transport),
        QString::fromStdString((store->file().parent_path() / "ndi").string()));
}

}  // namespace

void SessionFacade::load_libraries(const std::filesystem::path& data_dir)
{
    open_library_.load_registry(data_dir);
    apply_ndi_transport(open_library_);
}

bool SessionFacade::init_libraries(std::filesystem::path data_dir)
{
    load_libraries(data_dir);
    return open_active_library();
}

bool SessionFacade::open_active_library()
{
    const auto* registry = open_library_.registry();
    const auto* entry = registry->active();
    if (entry == nullptr) {
        // Only a refused list leaves none active: ensure_default makes one
        // otherwise.
        open_library_.set_last_error(QString::fromStdString(registry->load_error()));
        library_reg_->refresh();
        return false;
    }
    const bool ok = open_library(*entry);
    library_reg_->refresh();
    profile_ctl_->refresh();
    return ok;
}

bool SessionFacade::open_library(const library::LibraryEntry& entry)
{
    std::error_code ec;
    std::filesystem::create_directories(entry.media_root(), ec);
    return open_database(entry.database_file(), entry.id);
}

void SessionFacade::close_library()
{
    if (!open_library_.is_open()) {
        return;
    }
    // A desktop window moved a moment ago is saved with this library's
    // destinations, before another's are loaded over them.
    if (host_ != nullptr) {
        host_->flush_edits();
    }
    // Thumbnail warmers and background copies write into this library's
    // media folder. (Media imports stop when the library detaches.)
    media_->cancel_thumb_warmers();
    themes_->cancel_asset_copies();
    // Conversions and downloads write there too, and exports read its
    // pictures and media files.
    tools_->jobs().cancel_all();
    exporter_->cancel_exports();
    freeform_import_->library_closing();
    // The active profile always mirrors the live tables, so flush it before the
    // database it lives in goes away.
    profile_ctl_->capture_active_profile();
    mixer_->close_library();

    slide_edit_->cancel();

    // Drop live content first - mpv still holds files under the outgoing media root.
    session_.clear_all_stacks();
    session_.open_service(std::nullopt, {}, {});
    playback_->sync_backends();
    playback_->sync_background_video({});
    web_sources_->sync();
    live_sources_->sync();

    release_library();
    emit readyChanged();
}

void SessionFacade::release_library()
{
    const bool was_read_only = open_library_.read_only();
    open_library_.stop_profiles();
    library_->detach();
    media_->detach();
    freeform_library_->detach();
    web_library_->detach();
    open_library_.close();
    set_media_root({});
    fonts_->library_closed();

    themes_->clear_previews();
    previews_.clear_image_cache();

    // Repositories are null now, so both refreshes empty their models.
    library_->refresh_services();
    refresh_library();
    emit library_->bibleBrowseChanged();
    // Not ready before readOnly changes: what follows it sees no library open.
    ready_ = false;
    if (was_read_only) {
        emit readOnlyChanged();
    }
}

void SessionFacade::set_read_only(bool read_only)
{
    if (read_only == open_library_.read_only() || !open_library_.is_open()) {
        return;
    }
    open_library_.set_read_only(read_only);
    if (!read_only && ready_) {
        write_open_repairs();
    }
    emit readOnlyChanged();
}

void SessionFacade::write_open_repairs()
{
    try {
        if (outputs_unwritten_) {
            save_outputs();
            outputs_unwritten_ = false;
        }
        open_library_.seed_profiles();
        profile_ctl_->refresh();
    } catch (const std::exception& ex) {
        emit errorOccurred(report_entry_error(__func__, ex));
    }
}

bool SessionFacade::anything_live() const
{
    return !session_.occupied_stacks().empty();
}

void SessionFacade::reload_after_sync(const sync::Changed& changed)
{
    if (!ready_) {
        return;
    }
    const auto touched = [&](std::initializer_list<std::string_view> tables) {
        return std::ranges::any_of(tables, [&](std::string_view t) { return changed.touches(t); });
    };
    if (changed.touches("font_files")) {
        fonts_->library_fonts_changed();
    }
    const bool themes_changed = touched({"outputs", "app_settings", "style_profiles"});
    if (themes_changed) {
        // Held here and written back whole: read again before any save.
        session_.set_outputs(load_library_outputs(false));
        load_outputs_off();
        load_app_settings();
        themes_->library_changed();
        profile_ctl_->refresh();
        push_output_names();
        sync_output_host_state();
    }
    // Each list by what it shows. A library holds thousands of songs, so
    // only those changed are read again.
    if (changed.whole("songs")) {
        library_->refresh_songs();
    } else if (changed.touches("songs")) {
        library_->refresh_songs(changed.keys("songs"));
    }
    if (touched({"bibles", "verses"})) {
        library_->refresh_bibles();
    }
    for (const auto kind : db::k_media_kinds) {
        const auto tables = db::media_tables(kind);
        if (touched({tables.files, tables.folders, tables.items})) {
            media_->refresh(kind);
        }
    }
    // The Freeform tiles, drawn in the outputs' themes, were built again
    // with the themes above.
    if (!themes_changed && touched({"freeform_folders", "freeform_items"})) {
        freeform_library_->rebuild_freeform_browser();
    }
    if (touched({"web_folders", "web_pages"})) {
        web_library_->rebuild_browser();
    }
    if (touched({"services", "service_items"})) {
        library_->refresh_services();
        // The rundown open, as it is now (unless it went).
        if (const auto open = openServiceId(); open >= 0) {
            library_->openService(open);
        }
    }
    after_mutation(true);
}

void SessionFacade::media_arrived(const std::vector<std::string>& paths)
{
    if (!ready_) {
        return;
    }
    std::map<db::MediaKind, std::vector<std::string>> files;
    std::vector<std::string> pictures;
    bool theme_pictures = false;
    bool fonts = false;
    for (const auto& path : paths) {
        const auto slash = path.find('/');
        if (slash == std::string::npos) {
            continue;
        }
        const std::string_view folder(path.data(), slash);
        auto name = path.substr(slash + 1);
        if (folder == model::k_fonts_subdir) {
            fonts = true;
        } else if (folder == model::k_theme_backgrounds_subdir) {
            theme_pictures = true;
            pictures.push_back("/static/" + path);
        }
        for (const auto kind : db::k_media_kinds) {
            if (folder == db::media_subdir(kind)) {
                if (kind == db::MediaKind::Image) {
                    pictures.push_back("/static/" + path);
                }
                files[kind].push_back(std::move(name));
                break;
            }
        }
    }
    if (fonts) {
        fonts_->files_may_have_arrived();
    }
    // A picture found missing is looked for again by everything drawing one:
    // the outputs, and the previews and thumbnails drawn without it, which
    // ask again. Only those: files often come hundreds at a time. A theme's
    // thumbnail on the disk names the pictures it was drawn with, and is
    // looked at again for any theme picture.
    bool redrawn = false;
    if (!pictures.empty()) {
        redrawn = previews_.forget_missing_pictures(pictures);
        if (host_ != nullptr) {
            host_->forget_missing_pictures(pictures);
        }
        if (redrawn || theme_pictures) {
            themes_->library_changed();
        }
    }
    for (auto& [kind, names] : files) {
        media_->files_arrived(kind, names);
    }
    // A video or sound waiting for its file plays once it is here, and the
    // previews drawn without a picture are drawn again.
    if (redrawn || files.contains(db::MediaKind::Video) || files.contains(db::MediaKind::Audio)) {
        after_mutation(false);
    }
}

void SessionFacade::apply_profile_switch()
{
    // activate() already rewrote output category defaults in place plus the
    // per-item theme maps, verse orders, and theme priority in the database.
    load_app_settings();
    refresh_library();
    session_.rebuild();
    themes_->library_changed();
    after_mutation(true);
    profile_ctl_->refresh();
}

std::vector<model::OutputConfig> SessionFacade::load_library_outputs(bool made_now)
{
    auto* config = open_library_.config();
    auto outputs = config->load_outputs();
    // Only a library made here from nothing starts with an output. One that
    // has none keeps none: a library joined from another computer gets the
    // others' outputs as they arrive, and one made here would go to them all.
    bool changed = false;
    if (made_now && outputs.empty()) {
        outputs.push_back(make_default_main());
        changed = true;
    }
    // A library this computer only views is put right here and not written:
    // the computers that edit it write it.
    changed = prepare_outputs(outputs) || changed;
    outputs_unwritten_ = changed && open_library_.read_only();
    if (changed && !outputs_unwritten_) {
        config->save_outputs(outputs);
    }
    return outputs;
}

bool SessionFacade::prepare_outputs(std::vector<model::OutputConfig>& outputs)
{
    bool changed = model::ensure_output_ids(outputs);
    for (auto& oc : outputs) {
        changed = theme::seed_default_themes(oc) || changed;
    }
    return changed;
}

namespace {

/// The library settings the Settings dialog edits, by QML name. Stacks and
/// the stack each content type plays on are edited through StackController
/// only.
constexpr QmlField k_app_setting_fields[] = {
    {"organizationName", model::k_setting_organization_name},
    {"ccliLicenceNumber", model::k_setting_ccli_licence},
    {"themePriority", model::k_setting_theme_priority},
};

/// This computer's settings the dialog edits, by QML name.
constexpr QmlField k_machine_setting_fields[] = {
    {"remoteEnabled", model::k_setting_remote_enabled},
    {"remotePin", model::k_setting_remote_pin},
    {"remotePort", model::k_setting_remote_port},
};

}  // namespace

QVariantMap SessionFacade::getAppSettings()
{
    auto fields = to_qml_fields(open_library_.settings().to_json(), k_app_setting_fields);
    fields.insert(
        to_qml_fields(open_library_.machine_settings().to_json(), k_machine_setting_fields));
    return fields;
}

QString SessionFacade::updateAppSettings(const QVariantMap& fields)
try {
    auto* config = open_library_.config();
    if (config == nullptr) {
        return refusal::no_library();
    }

    auto machine = open_library_.machine_settings().to_json();
    apply_qml_fields(machine, fields, k_machine_setting_fields);
    auto next_machine = model::MachineSettings::from_json(machine);
    if (next_machine != open_library_.machine_settings()
        && !open_library_.save_machine_settings(std::move(next_machine))) {
        const auto* store = open_library_.machine_store();
        return tr("Could not save this computer's settings to %1")
            .arg(store ? QString::fromStdString(store->file().string()) : QString());
    }

    auto& settings = open_library_.settings();
    auto merged = settings.to_json();
    apply_qml_fields(merged, fields, k_app_setting_fields);
    auto next = model::AppSettings::from_json(merged);

    config->save_settings(next.to_json());

    const bool ccli_changed = next.ccli_licence_number != settings.ccli_licence_number;
    const bool priority_changed = next.theme_priority != settings.theme_priority;
    settings = std::move(next);
    apply_app_settings_live(ccli_changed || priority_changed);
    emit appSettingsChanged();
    return {};
} catch (const std::exception& ex) {
    return report_entry_error(__func__, ex);
}

QString SessionFacade::ndiTransport() const
{
    return QString::fromUtf8(model::ndi_transport_key(open_library_.machine_settings().ndi_transport));
}

bool SessionFacade::ndiTransportPending() const
{
    return render::ndi_transport_in_effect()
           != render_transport(open_library_.machine_settings().ndi_transport);
}

QString SessionFacade::setNdiTransport(const QString& transport)
{
    auto next = open_library_.machine_settings();
    next.ndi_transport = model::ndi_transport_from_key(transport.toStdString());
    if (next == open_library_.machine_settings()) {
        return {};
    }
    if (!open_library_.save_machine_settings(std::move(next))) {
        const auto* store = open_library_.machine_store();
        return tr("Could not save this computer's settings to %1")
            .arg(store ? QString::fromStdString(store->file().string()) : QString());
    }
    apply_ndi_transport(open_library_);
    emit appSettingsChanged();
    return {};
}

QStringList SessionFacade::list_remote_candidate_urls(int port) const
{
    QStringList urls;
    port = model::normalize_remote_port(port);
    for (const QString& addr : lan_addresses()) {
        urls.push_back(QStringLiteral("http://%1:%2/").arg(addr).arg(port));
    }
    return urls;
}

void SessionFacade::load_app_settings()
{
    open_library_.load_settings();
    apply_app_settings_live(false);
    emit appSettingsChanged();
}

void SessionFacade::apply_app_settings_live(bool rebuild)
{
    const auto& settings = open_library_.settings();
    session_.set_ccli_licence_number(settings.ccli_licence_number);
    session_.set_theme_priority(settings.theme_priority);
    stack_ctl_->apply_settings();
    if (rebuild) {
        session_.rebuild();
        after_mutation(true);
    }
}

void SessionFacade::refresh_library()
{
    library_->refresh_songs();
    library_->refresh_bibles();
    media_->refresh();
    freeform_library_->rebuild_freeform_browser();
    web_library_->rebuild_browser();
}

void SessionFacade::sweep_import_staging() const
{
    // A copy cut short by a crash leaves its staging file where the file was
    // going (see FileImporter), and a thumbnail write its temp file.
    const auto& root = open_library_.media_root();
    if (root.empty()) {
        return;
    }
    for (const auto kind : db::k_media_kinds) {
        FileImporter::sweep_staging(root / db::media_subdir(kind));
    }
    FileImporter::sweep_staging(theme::theme_assets_dir(root));
    util::remove_stale_cache_temps(media_thumbs_dir(root));
    QThreadPool::globalInstance()->start([root] { prune_theme_thumbs(root); });
}

void SessionFacade::set_media_root(std::filesystem::path root)
{
    open_library_.set_media_root(std::move(root));
    const auto& media_root = open_library_.media_root();
    if (host_ != nullptr) {
        host_->set_static_root(media_root);
    }
    previews_.set_media_root(media_root);
}

} // namespace seventhslide::app
