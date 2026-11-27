#include "mixer_controller.hpp"

#include <seventhslide/audio/audio_engine.hpp>
#include <seventhslide/media/audio_levels.hpp>
#include <seventhslide/model/output_config.hpp>

#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace seventhslide::app {
namespace {

std::optional<audio::StripKind> strip_kind(const QString& kind)
{
    if (kind == QStringLiteral("stack")) {
        return audio::StripKind::Stack;
    }
    if (kind == QStringLiteral("output")) {
        return audio::StripKind::Output;
    }
    return std::nullopt;
}

// Which tab shows follows the machine, as the media browser's view does.
const QString k_tab_setting = QStringLiteral("audioMixer/tab");

/// ``outputs`` stays ``outputs``; anything else (unset included) is ``stacks``.
QString normalized_tab(const QString& tab)
{
    return tab == QLatin1String("outputs") ? QStringLiteral("outputs") : QStringLiteral("stacks");
}

bool contains(const std::vector<std::string>& ids, const std::string& id)
{
    return std::ranges::find(ids, id) != ids.end();
}

std::vector<std::string> output_ids(const audio::Routing& routing)
{
    std::vector<std::string> ids;
    ids.reserve(routing.outputs.size());
    for (const auto& output : routing.outputs) {
        ids.push_back(output.id);
    }
    return ids;
}

} // namespace

MixerController::MixerController(session::Session& session, OpenLibrary& library, QObject* parent)
    : QObject(parent)
    , session_(session)
    , library_(library)
    , engine_(std::make_unique<audio::AudioEngine>())
    , stacks_(new MixerChannelModel(this))
    , outputs_(new MixerChannelModel(this))
    , save_timer_(new QTimer(this))
    , meter_timer_(new QTimer(this))
    , shown_tab_(normalized_tab(QSettings().value(k_tab_setting).toString()))
{
    meter_timer_->setInterval(k_meter_interval_ms);
    connect(meter_timer_, &QTimer::timeout, this, &MixerController::metersTick);
    save_timer_->setSingleShot(true);
    save_timer_->setInterval(k_save_delay_ms);
    connect(save_timer_, &QTimer::timeout, this, [this]() { (void)save_now(); });
    engine_->start_timer();
}

MixerController::~MixerController()
{
    if (save_timer_->isActive()) {
        save_timer_->stop();
        auto* store = store_for_library();
        if (store != nullptr && !store->save(library_id_, mix_)) {
            qWarning("SeventhSlide: could not save the Audio Mixer's levels to %s",
                     store->file().string().c_str());
        }
    }
    engine_->stop();
}

library::AudioMixStore* MixerController::store_for_library() noexcept
{
    return library_id_.empty() ? nullptr : library_.audio_mix_store();
}

void MixerController::open_library(std::string library_id)
{
    flush();
    library_id_ = std::move(library_id);
    auto* store = store_for_library();
    std::optional<library::LibraryMix> kept;
    if (store != nullptr) {
        kept = store->load(library_id_);
    }
    if (kept) {
        mix_ = std::move(*kept);
    } else {
        std::vector<std::string> ids;
        for (const auto& output : session_.outputs()) {
            ids.push_back(output.id);
        }
        mix_ = library::LibraryMix::first_use(ids);
        // Kept at once: closing the library before any change must not
        // bring the first-use rule back next time.
        if (store != nullptr) {
            (void)save_now();
        }
    }
    set_every_strip_ = true;
    sync();
}

void MixerController::close_library()
{
    flush();
    library_id_.clear();
    mix_ = {};
    set_every_strip_ = true;
}

void MixerController::sync()
{
    audio::Routing routing;
    std::vector<MixerChannelModel::Row> stack_rows;
    const auto previews = session_.preview_stack_ids();
    for (const auto& stack : session_.stack_defs()) {
        routing.stacks.push_back(stack.id);
        stack_rows.push_back({stack.id, QString::fromStdString(stack.name),
                              QString::fromStdString(stack.icon), {}});
        auto cue = audio::cue_strip_id(stack.id);
        // Its Preview's sound, under it.
        if (previews.contains(stack.id)) {
            stack_rows.push_back({cue, tr("%1: Preview").arg(QString::fromStdString(stack.name)),
                                  QString::fromStdString(stack.icon), {}, true});
            routing.cues.push_back(std::move(cue));
        }
    }
    std::vector<MixerChannelModel::Row> output_rows;
    // An output switched off on this computer has no strip and carries no
    // sound; its mix is kept for when it is on again.
    std::vector<std::string> outputs_kept;
    for (const auto& output : session_.outputs()) {
        outputs_kept.push_back(output.id);
        if (session_.outputs_off().contains(output.id)) {
            continue;
        }
        audio::Routing::Output carried{output.id, {}, output.disabled_content_types};
        for (const auto& stack : routing.stacks) {
            if (model::output_enables_stack(output.enabled_stack_ids, stack)) {
                carried.stacks.push_back(stack);
            }
        }
        routing.outputs.push_back(std::move(carried));
        output_rows.push_back({output.id, QString::fromStdString(output.name),
                               QString::fromStdString(output.icon), {}});
    }

    const bool every = std::exchange(set_every_strip_, false);
    bool pruned = false;
    if (!every) {
        // Gone since the last sync, in the same library: deleted (one
        // switched off is not gone), with its cue strip's settings, Preview
        // on or not (Preview off keeps them).
        for (const auto& id : routing_.stacks) {
            if (!contains(routing.stacks, id)) {
                pruned = mix_.stacks.erase(id) > 0 || pruned;
                pruned = mix_.stacks.erase(audio::cue_strip_id(id)) > 0 || pruned;
            }
        }
        for (const auto& id : output_ids(routing_)) {
            if (!contains(outputs_kept, id)) {
                pruned = mix_.outputs.erase(id) > 0 || pruned;
            }
        }
    }

    // The engine keeps strips by id across libraries, and every library has a
    // stack "main": every strip is set when a library opens, the defaults
    // included, and a new one when it first appears.
    auto& mixer = engine_->mixer();
    const auto before_outputs = output_ids(routing_);
    for (auto& row : stack_rows) {
        row.mix = mix_.stack(row.id);
        if (every || !contains(row.cue ? routing_.cues : routing_.stacks, row.id)) {
            mixer.set_strip(audio::StripKind::Stack, row.id, row.mix);
        }
    }
    for (auto& row : output_rows) {
        row.mix = mix_.output(row.id);
        if (every || !contains(before_outputs, row.id)) {
            mixer.set_strip(audio::StripKind::Output, row.id, row.mix);
        }
    }
    if (routing != routing_) {
        mixer.set_routing(routing);
        routing_ = std::move(routing);
    }
    stacks_->set_rows(std::move(stack_rows));
    outputs_->set_rows(std::move(output_rows));
    if (pruned) {
        schedule_save();
    }
}

void MixerController::schedule_save()
{
    if (store_for_library() != nullptr) {
        save_timer_->start();
    }
}

void MixerController::flush()
{
    if (save_timer_->isActive()) {
        save_timer_->stop();
        (void)save_now();
    }
}

bool MixerController::save_now()
{
    auto* store = store_for_library();
    if (store == nullptr) {
        return true;
    }
    if (!store->save(library_id_, mix_)) {
        emit errorOccurred(tr("Could not save the Audio Mixer's levels to %1")
                               .arg(QString::fromStdString(store->file().string())));
        return false;
    }
    return true;
}

template <typename Edit>
QString MixerController::change_strip(const QString& kind, const QString& id, Edit edit)
{
    const auto which = strip_kind(kind);
    if (!which) {
        return tr("The Audio Mixer has no “%1” strips.").arg(kind);
    }
    const bool stack = *which == audio::StripKind::Stack;
    auto* model = stack ? stacks_ : outputs_;
    const auto key = id.toStdString();
    if (!model->contains(key)) {
        return stack ? tr("No stack %1 in the Audio Mixer").arg(id)
                     : tr("No output %1 in the Audio Mixer").arg(id);
    }
    const library::ChannelMix before = stack ? mix_.stack(key) : mix_.output(key);
    library::ChannelMix mix = before;
    edit(mix);
    mix.gain_db = media::normalize_gain_db(mix.gain_db);
    if (mix == before) {
        return {};
    }
    (stack ? mix_.stacks : mix_.outputs)[key] = mix;
    engine_->mixer().set_strip(*which, key, mix);
    model->set_mix(key, mix);
    schedule_save();
    return {};
}

QString MixerController::setGainDb(const QString& kind, const QString& id, double db)
{
    // A level that is not a number is a mistake in the caller, and read as
    // one (0 dB) it would be the loudest there is.
    if (std::isnan(db)) {
        return tr("Not a level: %1").arg(db);
    }
    return change_strip(kind, id, [db](library::ChannelMix& mix) { mix.gain_db = db; });
}

QString MixerController::setMuted(const QString& kind, const QString& id, bool muted)
{
    return change_strip(kind, id, [muted](library::ChannelMix& mix) { mix.muted = muted; });
}

QString MixerController::setListen(const QString& kind, const QString& id, bool listen)
{
    return change_strip(kind, id, [listen](library::ChannelMix& mix) { mix.listen = listen; });
}

double MixerController::faderDb(double position) const
{
    return media::fader_db(position);
}

void MixerController::start_listen_device()
{
    if (engine_->start()) {
        qInfo("SeventhSlide: Listen plays on %s", engine_->device_name().c_str());
    } else {
        qWarning("SeventhSlide: no sound device opened; the mixer runs on its timer");
    }
}

void MixerController::setShownTab(const QString& tab)
{
    const auto next = normalized_tab(tab);
    if (shown_tab_ == next) {
        return;
    }
    shown_tab_ = next;
    QSettings().setValue(k_tab_setting, shown_tab_);
    emit shownTabChanged();
}

double MixerController::meterPosition(double db) const
{
    return media::meter_position(db);
}

audio::MeterReading MixerController::meter(audio::StripKind kind, std::string_view id) const
{
    return engine_->mixer().meter(kind, id);
}

void MixerController::add_meter_view()
{
    if (meter_views_++ == 0) {
        meter_timer_->start();
    }
}

void MixerController::remove_meter_view()
{
    if (meter_views_ > 0 && --meter_views_ == 0) {
        meter_timer_->stop();
    }
}

bool MixerController::meters_running() const
{
    return meter_timer_->isActive();
}

} // namespace seventhslide::app
