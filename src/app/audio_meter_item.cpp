#include "audio_meter_item.hpp"

#include "mixer_controller.hpp"

#include <seventhslide/media/audio_levels.hpp>

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <limits>

namespace seventhslide::app {
namespace {

audio::MeterReading silence()
{
    audio::MeterReading r{};
    r.level_db.fill(-std::numeric_limits<float>::infinity());
    r.peak_hold_db.fill(-std::numeric_limits<float>::infinity());
    return r;
}

bool same(const audio::MeterReading& a, const audio::MeterReading& b)
{
    return a.level_db == b.level_db && a.peak_hold_db == b.peak_hold_db;
}

} // namespace

AudioMeterItem::AudioMeterItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
    , reading_(silence())
{
    setAntialiasing(false);
}

AudioMeterItem::~AudioMeterItem()
{
    if (counted_) {
        counted_->remove_meter_view();
    }
}

void AudioMeterItem::setMixer(MixerController* mixer)
{
    if (mixer_ == mixer) {
        return;
    }
    mixer_ = mixer;
    emit mixerChanged();
    follow();
    read();
}

void AudioMeterItem::setKind(const QString& kind)
{
    if (kind_ == kind) {
        return;
    }
    kind_ = kind;
    emit kindChanged();
    read();
}

void AudioMeterItem::setChannelId(const QString& id)
{
    if (channel_id_ == id) {
        return;
    }
    channel_id_ = id;
    emit channelIdChanged();
    read();
}

void AudioMeterItem::setDimmed(bool dimmed)
{
    if (dimmed_ == dimmed) {
        return;
    }
    dimmed_ = dimmed;
    emit dimmedChanged();
    update();
}

void AudioMeterItem::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange) {
        follow();
    }
}

void AudioMeterItem::follow()
{
    const bool want = mixer_ && isVisible() && window() != nullptr;
    if (counted_ && (counted_ != mixer_ || !want)) {
        disconnect(counted_, &MixerController::metersTick, this, &AudioMeterItem::read);
        counted_->remove_meter_view();
        counted_ = nullptr;
    }
    if (want && !counted_) {
        counted_ = mixer_;
        connect(counted_, &MixerController::metersTick, this, &AudioMeterItem::read);
        counted_->add_meter_view();
        read();
    }
}

void AudioMeterItem::read()
{
    const auto next = mixer_ && !channel_id_.isEmpty()
                          ? mixer_->meter(kind_ == QLatin1String("output") ? audio::StripKind::Output
                                                                           : audio::StripKind::Stack,
                                          channel_id_.toStdString())
                          : silence();
    if (same(next, reading_)) {
        return;
    }
    reading_ = next;
    update();
}

void AudioMeterItem::paint(QPainter* painter)
{
    const double w = width();
    const double h = height();
    if (w <= 0.0 || h <= 0.0) {
        return;
    }
    constexpr double gap = 1.0;
    const double bar = std::max(1.0, (w - gap) / 2.0);
    const double mid = media::meter_position(k_mid_db);
    const double high = media::meter_position(k_high_db);
    const auto zone_color = [&](double at) {
        if (dimmed_) {
            return muted_color_;
        }
        return at >= high ? high_color_ : (at >= mid ? mid_color_ : low_color_);
    };
    // A band of the bar from ``from`` to ``to`` (0 at the floor, 1 at full
    // scale), in pixels from the top.
    const auto band = [&](double x, double from, double to) {
        const double top = std::round(h * (1.0 - to));
        const double bottom = std::round(h * (1.0 - from));
        return QRectF(x, top, bar, bottom - top);
    };

    for (int c = 0; c < audio::k_channels; ++c) {
        const double x = c * (bar + gap);
        painter->fillRect(QRectF(x, 0.0, bar, h), track_color_);
        const double level = media::meter_position(reading_.level_db[c]);
        if (level > 0.0) {
            const double zones[][2] = {{0.0, mid}, {mid, high}, {high, 1.0}};
            for (const auto& zone : zones) {
                const double to = std::min(level, zone[1]);
                if (to > zone[0]) {
                    painter->fillRect(band(x, zone[0], to), zone_color(zone[0]));
                }
            }
        }
        const double hold = media::meter_position(reading_.peak_hold_db[c]);
        if (hold > 0.0) {
            const double y = std::min(h - 1.0, std::round(h * (1.0 - hold)));
            painter->fillRect(QRectF(x, y, bar, 1.0), zone_color(hold));
        }
    }
}

} // namespace seventhslide::app
