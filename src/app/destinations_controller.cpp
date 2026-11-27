// The destinations each output paints to - displays, NDI, virtual cameras and
// Blackmagic cards - and the runtimes behind them. A QML adapter over
// render::OutputWindowHost, which owns the sinks themselves.

#include "destinations_controller.hpp"

#include "destination_rows.hpp"
#include "refusals.hpp"

#include <seventhslide/audio/device_sink.hpp>
#include <seventhslide/model/output_config.hpp>
#include <seventhslide/model/output_library.hpp>
#include <seventhslide/render/bmd_runtime.hpp>
#include <seventhslide/render/ndi_runtime.hpp>
#include <seventhslide/render/output_window_host.hpp>
#include <seventhslide/render/virtual_camera.hpp>
#include <seventhslide/stream/encoder_catalog.hpp>

#include <QFutureWatcher>
#include <QPromise>
#include <QThreadPool>

#include <memory>

namespace seventhslide::app {

namespace {

/// No output host: the outputs are being set up again, or are going.
QString outputs_not_running()
{
    return DestinationsController::tr("The outputs are not running.");
}

} // namespace

DestinationsController::DestinationsController(const session::Session& session, QObject* parent)
    : QObject(parent)
    , session_(session)
    , displays_(this)
{
}

const model::OutputConfig* DestinationsController::find_output(const QString& output_id) const
{
    return model::find_output_by_id(session_.outputs(), output_id.toStdString());
}

void DestinationsController::set_output_host(render::OutputWindowHost* host)
{
    // By its handle: the host may be one going (its destroyed handler calls
    // here), and the session connects it to this controller too.
    disconnect(live_connection_);
    host_ = host;
    if (host_ == nullptr) {
        displays_.set_displays({});
    } else {
        live_connection_ = connect(host_, &render::OutputWindowHost::liveChanged, this,
                                   &DestinationsController::liveOnChanged);
    }
    emit liveOnChanged();
}

bool DestinationsController::liveOn() const
{
    return host_ != nullptr && host_->live();
}

void DestinationsController::setLiveOn(bool on)
{
    if (host_ == nullptr || host_->live() == on) {
        return;
    }
    host_->set_live(on);
    if (on) {
        // Opened, synced and painted as a restore is.
        emit restoreRequested();
    } else {
        emit outputStateChanged();
    }
    emit liveStateChanged();
}

bool DestinationsController::liveOffStops() const
{
    return host_ != nullptr && host_->live_off_stops_any();
}

QString DestinationsController::setIgnoreLive(const QString& dest_id, bool ignore)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_ignore_live(dest_id, ignore); },
        tr("The destination could not be changed."), Refresh::SyncAndPresent);
}

void DestinationsController::notify_changed()
{
    emit changed();
}

void DestinationsController::refreshDisplays()
{
    if (host_ == nullptr) {
        displays_.set_displays({});
        return;
    }
    displays_.set_displays(display_rows(*host_));
}

QVariantList DestinationsController::list(const QString& output_id)
{
    const auto* oc = find_output(output_id);
    if (host_ == nullptr || oc == nullptr) {
        return {};
    }
    refreshDisplays();
    return destination_rows(*host_, QString::fromStdString(oc->id));
}

QVariantMap DestinationsController::add(const QString& output_id, const QString& type)
{
    QVariantMap out;
    if (host_ == nullptr) {
        out.insert(QStringLiteral("error"), outputs_not_running());
        return out;
    }
    const auto* oc = find_output(output_id);
    if (oc == nullptr) {
        out.insert(QStringLiteral("error"), refusal::output_gone());
        return out;
    }
    const QString id = host_->add_destination(QString::fromStdString(oc->id), type);
    if (id.isEmpty()) {
        out.insert(QStringLiteral("error"), tr("The destination could not be added."));
        return out;
    }
    notify_changed();
    out.insert(QStringLiteral("id"), id);
    return out;
}

QString DestinationsController::remove(const QString& dest_id)
{
    if (host_ == nullptr) {
        return outputs_not_running();
    }
    if (dest_id.isEmpty() || !host_->remove_destination(dest_id)) {
        return refusal::destination_gone();
    }
    notify_changed();
    emit outputStateChanged();
    return {};
}

QString DestinationsController::move(const QString& dest_id, int step)
{
    // Only the list's order changes: nothing to re-sync or repaint.
    return apply_change(
        dest_id, [&] { return host_->move_destination(dest_id, step); },
        tr("The destination could not be moved."), Refresh::None);
}

QString DestinationsController::apply_change(const QString& dest_id,
                                             const std::function<bool()>& apply,
                                             const QString& failure,
                                             Refresh refresh)
{
    if (host_ == nullptr) {
        return outputs_not_running();
    }
    if (dest_id.isEmpty()) {
        return refusal::destination_gone();
    }
    if (!apply()) {
        const QString err = host_->last_error();
        return err.isEmpty() ? failure : err;
    }
    notify_changed();
    if (refresh == Refresh::None) {
        return {};
    }
    if (refresh == Refresh::Sync || refresh == Refresh::SyncAndPresent) {
        emit outputStateChanged();
    }
    if (refresh == Refresh::Present || refresh == Refresh::SyncAndPresent) {
        emit repaintRequested();
    }
    emit liveStateChanged();
    return {};
}

QString DestinationsController::setEnabled(const QString& dest_id, bool enabled)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_enabled(dest_id, enabled); },
        tr("The destination could not be changed."),
        // Nothing to paint to a destination that was just switched off.
        enabled ? Refresh::SyncAndPresent : Refresh::Sync);
}

QString DestinationsController::setScreen(const QString& dest_id, const QString& screen_key)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_screen(dest_id, screen_key); },
        tr("The destination could not be put on that display."), Refresh::SyncAndPresent);
}

QString DestinationsController::setCamera(const QString& dest_id, const QString& device_path)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_camera(dest_id, device_path); },
        tr("The destination could not be sent to that camera."), Refresh::SyncAndPresent);
}

QString DestinationsController::setName(const QString& dest_id, const QString& name)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_name(dest_id, name); },
        tr("The destination could not be changed."), Refresh::None);
}

QVariantList DestinationsController::types() const
{
    // The output editor asks first: an SRT stream added or shown later finds
    // the encoders tried already.
    probe_encoders_soon();
    return destination_type_rows();
}

void DestinationsController::probe_encoders_soon() const
{
    if (probing_encoders_ || (stream::known_encoders(stream::VideoCodec::H265)
                              && stream::known_encoders(stream::VideoCodec::H264))) {
        return;
    }
    probing_encoders_ = true;
    auto* self = const_cast<DestinationsController*>(this);
    auto promise = std::make_shared<QPromise<void>>();
    auto* watcher = new QFutureWatcher<void>(self);
    connect(watcher, &QFutureWatcherBase::finished, self, [self, watcher] {
        watcher->deleteLater();
        self->probing_encoders_ = false;
        emit self->streamCodecsReady();
    });
    watcher->setFuture(promise->future());
    promise->start();
    QThreadPool::globalInstance()->start([promise] {
        stream::probe_encoders();
        promise->finish();
    });
}

QString DestinationsController::setNdiName(const QString& dest_id,
                                           const QString& source_name)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_ndi_name(dest_id, source_name); },
        tr("The NDI source name could not be set."), Refresh::SyncAndPresent);
}

QString DestinationsController::setAlpha(const QString& dest_id, bool alpha)
{
    // Alpha is a property of the sink itself; the output state behind it is
    // unchanged, so this repaints without re-pushing.
    return apply_change(
        dest_id, [&] { return host_->set_destination_alpha(dest_id, alpha); },
        tr("The destination could not be changed."), Refresh::Present);
}

QString DestinationsController::setAudio(const QString& dest_id, bool audio)
{
    // The pictures are unchanged; the mixer's Outputs tab shows which
    // outputs send sound, so the output state is synced.
    return apply_change(
        dest_id, [&] { return host_->set_destination_audio(dest_id, audio); },
        tr("The destination could not be changed."), Refresh::Sync);
}

QString DestinationsController::setFormat(const QString& dest_id, int max_height, bool video)
{
    // The sinks resize (or stop drawing) themselves; the output state behind
    // them is unchanged, but sound alone may have turned the sound on.
    return apply_change(
        dest_id, [&] { return host_->set_destination_format(dest_id, max_height, video); },
        tr("The destination could not be changed."), Refresh::Sync);
}

QString DestinationsController::setStream(const QString& dest_id, const QVariantMap& changes)
{
    const auto* d = host_ != nullptr ? host_->registry().find(dest_id) : nullptr;
    if (d == nullptr) {
        return host_ == nullptr ? outputs_not_running() : refusal::destination_gone();
    }
    // Resolved first: a name the editor should not send is its own mistake,
    // not the host's last.
    const auto settings = stream_settings_from(changes, d->stream);
    if (!settings) {
        return tr("That is not a setting a stream has.");
    }
    return apply_change(
        dest_id, [&] { return host_->set_destination_stream(dest_id, *settings); },
        tr("The stream could not be changed."), Refresh::SyncAndPresent);
}

QVariantList DestinationsController::streamCodecs() const
{
    probe_encoders_soon();
    return stream_codec_rows();
}

QVariantMap DestinationsController::streamRates(const QString& dest_id, int width,
                                                int height) const
{
    const auto* d = host_ != nullptr ? host_->registry().find(dest_id) : nullptr;
    return d != nullptr ? stream_bitrate_row(d->stream, QSize(width, height)) : QVariantMap{};
}

QVariantList DestinationsController::sizeOptions(int width, int height) const
{
    return destination_size_rows(QSize(width, height));
}

QString DestinationsController::setAudioDevice(const QString& dest_id, const QString& device_id)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_audio_device(dest_id, device_id); },
        tr("The sound device could not be set."), Refresh::Sync);
}

QVariantList DestinationsController::soundDevices(const QString& dest_id) const
{
    QString screen;
    if (host_ != nullptr) {
        if (const auto* d = host_->registry().find(dest_id)) {
            screen = host_->devices().target_label(*d);
        }
    }
    return sound_device_rows(screen, audio::playback_devices());
}

QVariantMap DestinationsController::ndiRuntimeInfo() const
{
    return ndi_runtime_row();
}

QVariantMap DestinationsController::refreshNdiRuntime()
{
    if (host_ != nullptr) {
        host_->refresh_ndi_runtime();
    } else {
        seventhslide::render::ndi_runtime_refresh();
    }
    return ndiRuntimeInfo();
}

QString DestinationsController::setBmdDevice(const QString& dest_id,
                                             const QString& device_id)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_bmd_device(dest_id, device_id); },
        tr("The Blackmagic device could not be set."), Refresh::SyncAndPresent);
}

QString DestinationsController::setBmdMode(const QString& dest_id,
                                           const QString& mode_key)
{
    return apply_change(
        dest_id, [&] { return host_->set_destination_bmd_mode(dest_id, mode_key); },
        tr("The video mode could not be set."), Refresh::SyncAndPresent);
}

QVariantList DestinationsController::blackmagicDevices() const
{
    return blackmagic_device_rows(host_);
}

QVariantList DestinationsController::blackmagicModes(const QString& device_id) const
{
    return blackmagic_mode_rows(device_id);
}

QVariantMap DestinationsController::refreshBmdRuntime()
{
    if (host_ != nullptr) {
        host_->refresh_bmd_runtime();
    } else {
        seventhslide::render::bmd_runtime_refresh();
    }
    return bmd_runtime_row();
}

QVariantList DestinationsController::virtualCameras() const
{
    return virtual_camera_rows(host_);
}

} // namespace seventhslide::app
