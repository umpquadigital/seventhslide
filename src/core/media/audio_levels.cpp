#include "seventhslide/media/audio_levels.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace seventhslide::media {
namespace {

constexpr double k_silence = -std::numeric_limits<double>::infinity();

// OBS's logarithmic fader (libobs/obs-audio-controls.c, OBS_FADER_LOG):
// position p gives offset - (range + offset) * ((range + offset) / offset)^-p
// dB, which is the floor at the bottom and 0 dB at the top.
constexpr double k_taper_offset_db = 6.0;
constexpr double k_taper_span_db = -k_fader_floor_db + k_taper_offset_db;

} // namespace

double normalize_gain_db(double db) noexcept
{
    if (std::isnan(db)) {
        return 0.0;
    }
    if (db <= k_fader_floor_db) {
        return k_silence;
    }
    return std::min(db, 0.0);
}

double db_to_gain(double db) noexcept
{
    if (std::isnan(db)) {
        return 1.0;
    }
    return std::isinf(db) && db < 0.0 ? 0.0 : std::pow(10.0, db / 20.0);
}

double gain_to_db(double gain) noexcept
{
    return gain > 0.0 ? 20.0 * std::log10(gain) : k_silence;
}

double fader_db(double position) noexcept
{
    if (std::isnan(position) || position <= 0.0) {
        return k_silence;
    }
    if (position >= 1.0) {
        return 0.0;
    }
    return normalize_gain_db(
        k_taper_offset_db
        - k_taper_span_db * std::pow(k_taper_span_db / k_taper_offset_db, -position));
}

double fader_position(double db) noexcept
{
    db = normalize_gain_db(db);
    if (std::isinf(db)) {
        return 0.0;
    }
    if (db >= 0.0) {
        return 1.0;
    }
    const double p = (std::log10(k_taper_span_db) - std::log10(-db + k_taper_offset_db))
                   / (std::log10(k_taper_span_db) - std::log10(k_taper_offset_db));
    return std::clamp(p, 0.0, 1.0);
}

double meter_position(double db) noexcept
{
    if (std::isnan(db) || db <= k_meter_floor_db) {
        return 0.0;
    }
    return std::min(1.0, (db - k_meter_floor_db) / -k_meter_floor_db);
}

std::string format_gain_db(double db)
{
    db = normalize_gain_db(db);
    if (std::isinf(db)) {
        return "-inf dB";
    }
    // A level that rounds to zero reads 0.0, never -0.0.
    const double tenths = std::round(db * 10.0) / 10.0;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%.1f dB", tenths == 0.0 ? 0.0 : tenths);
    return std::string(buf);
}

} // namespace seventhslide::media
