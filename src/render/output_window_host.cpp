#include <seventhslide/render/output_window_host.hpp>

#include <seventhslide/audio/device_sink.hpp>

#include <seventhslide/render/bmd_output.hpp>
#include <seventhslide/render/bmd_runtime.hpp>
#include <seventhslide/render/ndi_output.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/output_window.hpp>
#include <seventhslide/render/srt_output.hpp>
#include <seventhslide/render/virtual_camera.hpp>

#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

namespace seventhslide::render {

OutputWindowHost::OutputWindowHost(QObject* parent)
    : QObject(parent)
{
    if (auto* app = qobject_cast<QGuiApplication*>(QGuiApplication::instance())) {
        connect(app, &QGuiApplication::screenAdded, this, &OutputWindowHost::on_screen_added);
        connect(app, &QGuiApplication::screenRemoved, this, &OutputWindowHost::on_screen_removed);
        for (QScreen* screen : QGuiApplication::screens()) {
            watch_screen(screen);
        }
        // Quitting (Command+Q, or the operator window closing), Qt asks every
        // window to close, and gives up quitting if one will not: an audience
        // window refuses a close not its host's. They go first.
        app->installEventFilter(this);
    }
    connect(&surfaces_, &OutputSurfaces::closeRequested, this,
            &OutputWindowHost::on_window_close_requested);
    connect(&surfaces_, &OutputSurfaces::windowPlaceChanged, this,
            &OutputWindowHost::on_window_place_changed);
    connect(&surfaces_, &OutputSurfaces::sinkStatusChanged, this,
            &OutputWindowHost::sinkStatusChanged);
    // Dragging a window moves it many times a second; it is saved once.
    place_save_.setSingleShot(true);
    place_save_.setInterval(500);
    connect(&place_save_, &QTimer::timeout, this, &OutputWindowHost::destinationsEdited);
    screens_settling_.setSingleShot(true);
    screens_settling_.setInterval(2000);
}

OutputWindowHost::~OutputWindowHost() = default;

OffscreenSink::Status OutputWindowHost::sink_status(const QString& dest_id) const
{
    const OffscreenSink* sink = surfaces_.sink(dest_id);
    return sink != nullptr ? sink->status() : OffscreenSink::Status{};
}

QHash<QString, QString> OutputWindowHost::live_targets(DestinationKind kind) const
{
    QHash<QString, QString> out;
    for (const auto& d : registry_.all()) {
        if (d.kind == kind && surfaces_.is_live(d)) {
            out.insert(d.target_key, d.output_id);
        }
    }
    return out;
}

void OutputWindowHost::destroy_destination(const QString& dest_id)
{
    surfaces_.close_window(dest_id);
    surfaces_.close_sink(dest_id);
    sync_display_sounds();
}

void OutputWindowHost::disable_others_on_target(DestinationKind kind,
                                                const QString& target_key,
                                                const QString& keep_id)
{
    for (const QString& id : registry_.others_on_target(kind, target_key, keep_id)) {
        registry_.find(id)->enabled = false;
        destroy_destination(id);
    }
}

QString OutputWindowHost::add_destination(const QString& output_id, const QString& type)
{
    const auto kind = destination_kind_from_name(type);
    if (output_id.isEmpty() || !kind || !destination_kind_available(*kind)) {
        return {};
    }
    const QString id = registry_.add(output_id, *kind).id;
    announce_edit();
    return id;
}

bool OutputWindowHost::remove_destination(const QString& dest_id)
{
    if (registry_.find(dest_id) == nullptr) {
        return false;
    }
    destroy_destination(dest_id);
    registry_.remove(dest_id);
    closed_.remove(dest_id);
    announce_edit();
    return true;
}

bool OutputWindowHost::move_destination(const QString& dest_id, int step)
{
    if (!registry_.move(dest_id, step)) {
        return false;
    }
    announce_edit();
    return true;
}

void OutputWindowHost::announce_edit()
{
    emit destinationsEdited();
    emit changed();
}

bool OutputWindowHost::commit(Destination& dest, bool reopen)
{
    bool ok = true;
    // An output switched off, or Live off, opens nothing; the edit is kept
    // for when it is on. Held back, it still takes its screen or device
    // from any other, as starting would: both must not open there later.
    if (reopen && !may_open(dest)) {
        disable_others_on_target(dest.kind, dest.target_key, dest.id);
    } else if (reopen && !start_destination(dest)) {
        // A start can refuse before replacing what was running (no runtime,
        // no device); a disabled destination must not keep sending.
        dest.enabled = false;
        destroy_destination(dest.id);
        ok = false;
    }
    announce_edit();
    return ok;
}

bool OutputWindowHost::set_destination_enabled(const QString& dest_id, bool enabled)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    dest->enabled = enabled;
    // Switched either way, a window closed opens again once it is on.
    closed_.remove(dest_id);
    if (!enabled) {
        destroy_destination(dest_id);
    }
    return commit(*dest, enabled);
}

bool OutputWindowHost::set_destination_ignore_live(const QString& dest_id, bool ignore)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    if (dest->ignore_live == ignore) {
        return true;
    }
    dest->ignore_live = ignore;
    if (live_) {
        // It sends either way.
        announce_edit();
        return true;
    }
    if (!ignore) {
        destroy_destination(dest_id);
        announce_edit();
        return true;
    }
    return commit(*dest, dest->enabled);
}

void OutputWindowHost::set_live(bool on)
{
    if (live_ == on) {
        return;
    }
    live_ = on;
    for (const auto& d : registry_.all()) {
        if (d.ignore_live) {
            continue;
        }
        // It follows Live: a window closed opens again with it.
        closed_.remove(d.id);
        if (!on && is_running(d)) {
            destroy_destination(d.id);
        }
    }
    emit liveChanged();
    emit changed();
}

bool OutputWindowHost::live_off_stops_any() const
{
    return live_ && std::ranges::any_of(registry_.all(), [this](const Destination& d) {
               return !d.ignore_live && is_running(d);
           });
}

bool OutputWindowHost::is_running(const Destination& dest) const
{
    return surfaces_.has_window(dest.id) || surfaces_.has_sink(dest.id);
}

bool OutputWindowHost::may_open(const Destination& dest) const
{
    return !output_is_off(dest.output_id) && (live_ || dest.ignore_live);
}

bool OutputWindowHost::set_destination_screen(const QString& dest_id, const QString& screen_key)
{
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr || dest->kind != DestinationKind::Display) {
        return false;
    }
    QScreen* screen = DeviceCatalog::screen_by_key(screen_key);
    if (!screen) {
        return false;
    }
    DeviceCatalog::apply_screen(*dest, screen);
    return commit(*dest, dest->enabled);
}

bool OutputWindowHost::set_destination_camera(const QString& dest_id, const QString& device_path)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr || dest->kind != DestinationKind::Vcam) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    if (device_path.isEmpty() || !is_virtual_camera_device(device_path)) {
        last_error_ = virtual_camera_missing_message();
        return false;
    }
    dest->target_key = device_path;
    dest->label = DeviceCatalog::camera_label(device_path);
    return commit(*dest, dest->enabled);
}

bool OutputWindowHost::set_destination_ndi_name(const QString& dest_id,
                                                const QString& source_name)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr || dest->kind != DestinationKind::Ndi) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    const QString clean = ndi_sanitize_source_name(source_name);
    if (clean.isEmpty()) {
        last_error_ = QStringLiteral("An NDI destination needs a source name.");
        return false;
    }
    if (registry_.ndi_name_taken(clean, dest_id)) {
        last_error_ =
            QStringLiteral("Another NDI destination is already called \"%1\".").arg(clean);
        return false;
    }
    if (dest->target_key == clean) {
        return true;
    }
    dest->target_key = clean;
    dest->label = clean;
    // The source name is baked into the sender at create time.
    return commit(*dest, dest->enabled);
}

bool OutputWindowHost::set_destination_alpha(const QString& dest_id, bool alpha)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr
        || (dest->kind != DestinationKind::Ndi && dest->kind != DestinationKind::Bmd)) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    if (dest->alpha == alpha) {
        return true;
    }
    dest->alpha = alpha;
    OffscreenSink* sink = surfaces_.sink(dest_id);
    // NDI changes the pixel format but not the sender, so a live stream
    // switches without receivers dropping. Blackmagic enables the keyer as
    // part of opening the output, so a running card restarts to pick it up.
    if (dest->kind == DestinationKind::Ndi && sink != nullptr) {
        sink->set_alpha_enabled(alpha);
    }
    return commit(*dest, dest->kind == DestinationKind::Bmd && sink != nullptr);
}

bool OutputWindowHost::set_destination_audio(const QString& dest_id, bool audio)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr || !destination_kind_carries_audio(dest->kind)) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    if (dest->audio == audio) {
        return true;
    }
    if (!audio && !dest->video) {
        last_error_ = QStringLiteral(
            "This destination sends sound alone. Turn Audio only off to stop its sound.");
        return false;
    }
    dest->audio = audio;
    // The sender stays up: receivers keep the picture while the sound starts
    // or stops.
    apply_audio(*dest);
    return commit(*dest, false);
}

bool OutputWindowHost::set_destination_format(const QString& dest_id, int max_height,
                                              bool video)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    if (max_height < 0 || (max_height > 0 && !destination_kind_sizable(dest->kind))) {
        last_error_ = QStringLiteral("This destination's size is not its own to choose.");
        return false;
    }
    if (!video && !destination_kind_sends_sound_alone(dest->kind)) {
        last_error_ = QStringLiteral("This destination cannot send sound alone.");
        return false;
    }
    if (dest->max_height == max_height && dest->video == video) {
        return true;
    }
    dest->max_height = max_height;
    dest->video = video;
    // Sound alone is the sound: never nothing at all.
    const bool audio_on = !video && !dest->audio;
    if (audio_on) {
        dest->audio = true;
    }
    // Live, as alpha is: an NDI sender keeps its receivers through a resize
    // or the picture stopping; a virtual camera renegotiates its format.
    surfaces_.apply_format(*dest);
    if (audio_on) {
        apply_audio(*dest);
    }
    return commit(*dest, false);
}

bool OutputWindowHost::set_destination_stream(const QString& dest_id,
                                              const stream::StreamSettings& settings)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr || dest->kind != DestinationKind::Srt) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    const std::string problem = stream::validate(settings);
    if (!problem.empty()) {
        last_error_ = QString::fromStdString(problem);
        return false;
    }
    if (dest->stream == settings) {
        return true;
    }
    dest->stream = settings;
    dest->target_key = QString::fromStdString(stream::address_text(settings));
    dest->label = dest->target_key;
    // Fixed for the length of a connection: a running stream starts again.
    return commit(*dest, dest->enabled);
}

void OutputWindowHost::set_audio_mixer(audio::Mixer* mixer)
{
    if (mixer_ == mixer) {
        return;
    }
    // The displays' sounds belong to the old mixer: they leave it first.
    display_sounds_.clear();
    mixer_ = mixer;
    for (const Destination& dest : registry_.all()) {
        apply_audio(dest);
    }
    sync_display_sounds();
}

bool OutputWindowHost::set_destination_audio_device(const QString& dest_id,
                                                    const QString& device_id)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr || !destination_kind_windowed(dest->kind)) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    if (dest->audio_device == device_id) {
        return true;
    }
    dest->audio_device = device_id;
    sync_display_sounds();
    return commit(*dest, false);
}

void OutputWindowHost::set_sound_backend(audio::AudioEngine::Backend backend)
{
    sound_backend_ = backend;
}

bool OutputWindowHost::display_sound_playing(const QString& dest_id) const
{
    const auto it = display_sounds_.find(dest_id);
    return it != display_sounds_.end() && it->second.sink->running();
}

void OutputWindowHost::sync_display_sounds(bool retry)
{
    const auto wanted = [this](const Destination* d) {
        return d != nullptr && mixer_ != nullptr && destination_kind_windowed(d->kind)
               && !d->audio_device.isEmpty() && surfaces_.has_window(d->id);
    };
    for (auto it = display_sounds_.begin(); it != display_sounds_.end();) {
        const Destination* d = registry_.find(it->first);
        const bool keep = wanted(d) && d->audio_device == it->second.device
                          && !(retry && !it->second.sink->running());
        it = keep ? std::next(it) : display_sounds_.erase(it);
    }
    for (const Destination& d : registry_.all()) {
        if (!wanted(&d) || display_sounds_.contains(d.id)) {
            continue;
        }
        auto sink = std::make_unique<audio::DeviceSink>(sound_backend_);
        // One that will not open is kept, silent, so it is not tried again
        // on every change; a restore or another choice tries again.
        (void)sink->start(*mixer_, d.output_id.toStdString(), d.audio_device.toStdString());
        display_sounds_.emplace(d.id, DisplaySound{d.audio_device, std::move(sink)});
    }
}

QStringList OutputWindowHost::outputs_sending_audio() const
{
    QStringList out;
    for (const Destination& dest : registry_.all()) {
        const OffscreenSink* sink = surfaces_.sink(dest.id);
        const bool sending =
            (sink != nullptr && sink->sending_audio()) || display_sound_playing(dest.id);
        if (sending && !out.contains(dest.output_id)) {
            out.push_back(dest.output_id);
        }
    }
    return out;
}

void OutputWindowHost::apply_audio(const Destination& dest)
{
    if (OffscreenSink* sink = surfaces_.sink(dest.id)) {
        sink->set_audio(mixer_, dest.output_id.toStdString(), dest.audio);
    }
}

bool OutputWindowHost::set_destination_bmd_device(const QString& dest_id,
                                                  const QString& device_id)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr || dest->kind != DestinationKind::Bmd) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    if (dest->target_key == device_id) {
        return true;
    }
    dest->target_key = device_id;
    dest->label = DeviceCatalog::bmd_device_label(device_id);
    // A mode from the previous card may not exist on this one. Clearing it
    // lets the sink pick a mode the new device actually offers.
    dest->mode_key.clear();
    return commit(*dest, dest->enabled);
}

bool OutputWindowHost::set_destination_bmd_mode(const QString& dest_id, const QString& mode_key)
{
    last_error_.clear();
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr || dest->kind != DestinationKind::Bmd) {
        last_error_ = QStringLiteral("Invalid destination");
        return false;
    }
    if (dest->mode_key == mode_key) {
        return true;
    }
    dest->mode_key = mode_key;
    // The mode is fixed while the output is open, so changing it means a
    // restart. That is a deliberate operator action, unlike a canvas resize.
    return commit(*dest, dest->enabled);
}

bool OutputWindowHost::set_destination_name(const QString& dest_id, const QString& name)
{
    Destination* dest = registry_.find(dest_id);
    if (dest == nullptr) {
        return false;
    }
    const QString trimmed = name.trimmed();
    if (dest->name == trimmed) {
        return true;
    }
    dest->name = trimmed;
    announce_edit();
    return true;
}

void OutputWindowHost::refresh_ndi_runtime()
{
    const bool before = ndi_runtime_available();
    ndi_runtime_refresh();
    if (ndi_runtime_available() != before) {
        emit changed();
    }
}

void OutputWindowHost::refresh_bmd_runtime()
{
    const bool before = bmd_runtime_available();
    bmd_runtime_refresh();
    if (bmd_runtime_available() != before) {
        emit changed();
    }
}

void OutputWindowHost::set_output_names(QHash<QString, QString> names_by_id)
{
    if (!registry_.set_output_names(std::move(names_by_id))) {
        return;
    }
    surfaces_.retitle([this](const QString& id) { return registry_.output_label(id); });
}

void OutputWindowHost::remove_output(const QString& output_id)
{
    if (output_id.isEmpty()) {
        return;
    }
    const QStringList removed = registry_.remove_output(output_id);
    if (removed.isEmpty()) {
        return;
    }
    for (const QString& id : removed) {
        destroy_destination(id);
        closed_.remove(id);
    }
    surfaces_.forget_output(output_id);
    announce_edit();
}

void OutputWindowHost::set_outputs_off(QSet<QString> output_ids)
{
    if (output_ids == outputs_off_) {
        return;
    }
    for (const auto& d : registry_.all()) {
        // Its output switched off or on: a window closed opens again with it.
        if (outputs_off_.contains(d.output_id) != output_ids.contains(d.output_id)) {
            closed_.remove(d.id);
        }
    }
    outputs_off_ = std::move(output_ids);
    bool closed = false;
    for (const auto& d : registry_.all()) {
        if (outputs_off_.contains(d.output_id)
            && (surfaces_.has_window(d.id) || surfaces_.has_sink(d.id))) {
            destroy_destination(d.id);
            closed = true;
        }
    }
    if (closed) {
        emit changed();
    }
}

bool OutputWindowHost::start_vcam_sink(Destination& dest)
{
    last_error_.clear();
    if (dest.target_key.isEmpty()) {
        dest.target_key = registry_.unused_camera_key();
        dest.label = DeviceCatalog::camera_label(dest.target_key);
    }
    if (dest.target_key.isEmpty() || !is_virtual_camera_device(dest.target_key)) {
        last_error_ = virtual_camera_missing_message();
        return false;
    }
    disable_others_on_target(DestinationKind::Vcam, dest.target_key, dest.id);
    surfaces_.close_sink(dest.id);

    auto* sink = new VirtualCameraSink(&surfaces_);
    // Named as its NDI source would be; before it starts, so the microphone
    // comes with the camera.
    sink->set_microphone_name(ndi_default_source_name(registry_.output_label(dest.output_id)));
    sink->set_audio(mixer_, dest.output_id.toStdString(), dest.audio);
    return surfaces_.start_sink(dest, sink, surfaces_.camera_size_for(dest.output_id), last_error_);
}

bool OutputWindowHost::start_ndi_sink(Destination& dest)
{
    last_error_.clear();
    if (!ndi_runtime_available()) {
        last_error_ = ndi_missing_message();
        return false;
    }
    if (dest.target_key.trimmed().isEmpty()) {
        dest.target_key = registry_.unused_ndi_name(dest.output_id, dest.id);
        dest.label = dest.target_key;
    }
    surfaces_.close_sink(dest.id);

    auto* sink = new NdiSink(&surfaces_);
    sink->set_alpha_enabled(dest.alpha);
    // Before it starts, so the sound starts with the sender.
    sink->set_audio(mixer_, dest.output_id.toStdString(), dest.audio);
    return surfaces_.start_sink(dest, sink, surfaces_.canvas_size_for(dest.output_id),
                                last_error_);
}

bool OutputWindowHost::start_bmd_sink(Destination& dest)
{
    last_error_.clear();
    if (!bmd_runtime_available()) {
        last_error_ = bmd_missing_message();
        return false;
    }
    if (dest.target_key.isEmpty()) {
        dest.target_key = registry_.unused_bmd_device();
        dest.label = DeviceCatalog::bmd_device_label(dest.target_key);
    }
    if (dest.target_key.isEmpty()) {
        last_error_ = bmd_no_device_message();
        return false;
    }
    // Only one process may hold a DeckLink output sub-device at a time; taking
    // it from another of our own destinations beats an opaque driver error.
    disable_others_on_target(DestinationKind::Bmd, dest.target_key, dest.id);
    surfaces_.close_sink(dest.id);

    auto* sink = new BmdSink(&surfaces_);
    sink->set_mode_key(dest.mode_key);
    sink->set_alpha_enabled(dest.alpha);
    // Before it starts, so the sound starts with the card.
    sink->set_audio(mixer_, dest.output_id.toStdString(), dest.audio);
    if (!surfaces_.start_sink(dest, sink, surfaces_.canvas_size_for(dest.output_id),
                              last_error_)) {
        return false;
    }
    // Record the mode actually opened, so a destination created before any
    // card was attached stops saying "let the card pick" once it has one.
    if (dest.mode_key.isEmpty()) {
        dest.mode_key = sink->active_mode().key;
    }
    return true;
}

bool OutputWindowHost::start_destination(Destination& dest)
{
    // Started on purpose (an edit that reopens it): no longer closed.
    closed_.remove(dest.id);
    switch (dest.kind) {
    case DestinationKind::Display:
        return open_dest_window(dest);
    case DestinationKind::Window:
        open_desktop_window(dest);
        return true;
    case DestinationKind::Vcam:
        return start_vcam_sink(dest);
    case DestinationKind::Ndi:
        return start_ndi_sink(dest);
    case DestinationKind::Bmd:
        return start_bmd_sink(dest);
    case DestinationKind::Srt:
        return start_srt_sink(dest);
    }
    return false;
}

bool OutputWindowHost::start_srt_sink(Destination& dest)
{
    last_error_.clear();
    if (!stream::srt_supported()) {
        last_error_ = QStringLiteral("This build of FFmpeg cannot send SRT.");
        return false;
    }
    // A port waits for one receiver, and a receiver takes one stream.
    disable_others_on_target(DestinationKind::Srt, dest.target_key, dest.id);
    surfaces_.close_sink(dest.id);

    auto* sink = new SrtSink(&surfaces_);
    sink->set_settings(dest.stream);
    sink->set_audio(mixer_, dest.output_id.toStdString(), dest.audio);
    return surfaces_.start_sink(dest, sink, surfaces_.canvas_size_for(dest.output_id),
                                last_error_);
}

bool OutputWindowHost::open_dest_window(Destination& dest)
{
    QScreen* screen = DeviceCatalog::match_screen(dest);
    if (!screen) {
        // Its screen is gone: one the operator is not on, if there is one.
        screen = DeviceCatalog::audience_screen();
    }
    if (!screen) {
        return false;
    }
    DeviceCatalog::apply_screen(dest, screen);
    disable_others_on_target(DestinationKind::Display, dest.target_key, dest.id);
    surfaces_.show_window(dest, registry_.output_label(dest.output_id), screen);
    sync_display_sounds();
    return true;
}

void OutputWindowHost::open_desktop_window(Destination& dest)
{
    QList<QRect> screens;
    for (QScreen* s : QGuiApplication::screens()) {
        screens.push_back(s->availableGeometry());
    }
    QScreen* home = DeviceCatalog::operator_screen();
    if (home == nullptr) {
        home = QGuiApplication::primaryScreen();
    }
    const QRect place =
        desktop_window_geometry(dest.bounds, surfaces_.canvas_size_for(dest.output_id), screens,
                                home != nullptr ? home->availableGeometry() : QRect());
    surfaces_.show_desktop_window(dest, registry_.output_label(dest.output_id), place);
    sync_display_sounds();
}

void OutputWindowHost::flush_edits()
{
    if (place_save_.isActive()) {
        place_save_.stop();
        emit destinationsEdited();
    }
}

void OutputWindowHost::shutdown()
{
    // A window moved a moment ago is kept where it was left.
    flush_edits();
    surfaces_.close_all();
    sync_display_sounds();
}

void OutputWindowHost::restore()
{
    restore_enabled_windows();
    sync_display_sounds(true);
    emit changed();
}

void OutputWindowHost::restore_enabled_windows()
{
    for (auto& d : registry_.all()) {
        if (!d.enabled || !may_open(d) || closed_.contains(d.id)) {
            continue;
        }
        if (destination_kind_offscreen(d.kind)) {
            if (!surfaces_.has_sink(d.id)) {
                // One that cannot open stays enabled; the next restore (a
                // screen added, the library reopened) tries again.
                (void)start_destination(d);
            }
            continue;
        }
        if (surfaces_.has_window(d.id)) {
            continue;
        }
        if (d.kind == DestinationKind::Window) {
            open_desktop_window(d);
            continue;
        }
        if (QScreen* screen = DeviceCatalog::match_screen(d)) {
            surfaces_.show_window(d, registry_.output_label(d.output_id), screen);
        }
    }
}

void OutputWindowHost::replace_destinations(const nlohmann::json& records)
{
    surfaces_.close_all();
    // Too late to save: the owner has another library open by now (it calls
    // flush_edits before closing the last).
    place_save_.stop();
    registry_.replace(records);
    closed_.clear();
    sync_display_sounds();
    emit changed();
}

bool OutputWindowHost::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Quit && watched == QCoreApplication::instance()) {
        shutdown();
    }
    return QObject::eventFilter(watched, event);
}

void OutputWindowHost::on_window_close_requested(const QString& destination_id)
{
    // Closed, not switched off: it stays enabled, and comes back with a
    // switch, Live or its output turned on again.
    if (registry_.find(destination_id) == nullptr || !surfaces_.has_window(destination_id)) {
        return;
    }
    closed_.insert(destination_id);
    destroy_destination(destination_id);
    emit changed();
}

void OutputWindowHost::on_window_place_changed(const QString& destination_id, QRect geometry)
{
    Destination* dest = registry_.find(destination_id);
    if (dest == nullptr || dest->kind != DestinationKind::Window || geometry.isEmpty()) {
        return;
    }
    // Moved by the system as screens came or went (a projector unplugged):
    // the operator's place is kept, for when its screen is back.
    if (screens_settling_.isActive()) {
        return;
    }
    const QString bounds = bounds_text(geometry);
    if (dest->bounds == bounds) {
        return;
    }
    dest->bounds = bounds;
    place_save_.start();
}

void OutputWindowHost::on_screen_removed(QScreen* screen)
{
    if (!screen) {
        return;
    }
    screens_settling_.start();
    surfaces_.close_windows_on(DeviceCatalog::screen_key(screen));
    sync_display_sounds();
    emit changed();
}

void OutputWindowHost::watch_screen(QScreen* screen)
{
    if (screen == nullptr) {
        return;
    }
    connect(screen, &QScreen::geometryChanged, this,
            [this, screen] { on_screen_geometry_changed(screen); });
}

void OutputWindowHost::on_screen_geometry_changed(QScreen* screen)
{
    screens_settling_.start();
    // A new resolution, or the displays rearranged: each window on it covers
    // it again, and its destination remembers the screen as it is now (its
    // key may hold the geometry), so it is not shown as missing.
    bool edited = false;
    for (const QString& id : surfaces_.refit_on(screen)) {
        Destination* dest = registry_.find(id);
        if (dest == nullptr) {
            continue;
        }
        const QString key = dest->target_key;
        const QString bounds = dest->bounds;
        const QString label = dest->label;
        DeviceCatalog::apply_screen(*dest, screen);
        edited = edited || dest->target_key != key || dest->bounds != bounds || dest->label != label;
    }
    if (edited) {
        announce_edit();
    } else {
        emit changed();
    }
}

void OutputWindowHost::restack()
{
    surfaces_.restack();
}

void OutputWindowHost::on_screen_added(QScreen* screen)
{
    screens_settling_.start();
    watch_screen(screen);
    restore_enabled_windows();
    surfaces_.refit();
    emit changed();
}

}  // namespace seventhslide::render
