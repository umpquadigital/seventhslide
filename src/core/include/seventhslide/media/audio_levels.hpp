#pragma once

// Audio levels as the mixer shows and applies them: decibels and linear gain,
// the fader's taper and the meter's scale. A fader runs from silence at the
// bottom to 0 dB at the top, with no boost; the taper is OBS Studio's
// logarithmic one, so a strip handles as an OBS fader does.
// Seam: plain C++20 (no Qt).

#include <string>

namespace seventhslide::media {

/// The fader's travel: anything below this is the bottom of the fader, silence.
inline constexpr double k_fader_floor_db = -96.0;
/// The meter's scale runs from here (its foot) to 0 dBFS (its head).
inline constexpr double k_meter_floor_db = -60.0;

/// ``db`` as a level the mixer accepts: at most 0 dB, -infinity (silence) at
/// or below ``k_fader_floor_db``, and 0 dB for NaN.
[[nodiscard]] double normalize_gain_db(double db) noexcept;

/// Linear amplitude for ``db``; 0 for -infinity, and 1 (0 dB) for NaN, as
/// ``normalize_gain_db`` reads it.
[[nodiscard]] double db_to_gain(double db) noexcept;
/// Decibels for linear amplitude ``gain``; -infinity for 0 or less.
[[nodiscard]] double gain_to_db(double gain) noexcept;

/// The level at fader position ``position`` (0 at the bottom, 1 at the top).
[[nodiscard]] double fader_db(double position) noexcept;
/// The fader position (0 to 1) that gives level ``db``.
[[nodiscard]] double fader_position(double db) noexcept;

/// How far up the meter ``db`` reaches (0 at ``k_meter_floor_db`` or below,
/// 1 at 0 dBFS or above).
[[nodiscard]] double meter_position(double db) noexcept;

/// A strip's readout: "0.0 dB", "-12.5 dB", "-inf dB".
[[nodiscard]] std::string format_gain_db(double db);

} // namespace seventhslide::media
