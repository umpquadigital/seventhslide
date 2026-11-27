#pragma once

// How an audio item's sound comes and goes on an output. Three tiers say so,
// field by field, in the library's theme priority (Settings > General): its
// "content" tier is the audio file's own (``audio_files.audio_fades``, the
// Library's Edit), "service" the open service's (``services.audio_fades``,
// Service settings' General page), and "global" the output's
// (OutputConfig::audio_fades, the output's General page). A file or a service
// sets only the fields it sets; the output has them all. Each output hears
// the item fade by its own: the Audio Mixer keeps the item's level on each
// output apart (``audio::Mixer::fade_source``). A video's sound is not this:
// it goes with its picture, over each output's change. Seam: plain C++20 +
// nlohmann/json.

#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <string_view>

namespace seventhslide::model {

/// The key an output keeps its fades under, and the column a service and an
/// audio file keep theirs in.
inline constexpr std::string_view k_audio_fades_key = "audio_fades";

/// The longest any of them may be.
inline constexpr int k_audio_fade_max_ms = 60'000;

/// Each 0 is a cut.
struct AudioFades {
    /// Up from nothing as it starts.
    int fade_in_ms = 0;
    /// Down to nothing as it stops or is cleared.
    int fade_out_ms = 0;
    /// Into the next audio item on the same stack: the two play together
    /// this long, one going as the other comes.
    int overlap_ms = 0;
    /// The transport's Pause fades the item out over ``fade_out_ms`` before
    /// it pauses, and its Play fades it in over ``fade_in_ms``. Otherwise
    /// they are immediate.
    bool on_play_pause = false;

    [[nodiscard]] bool operator==(const AudioFades&) const noexcept = default;

    /// Every field, each in 0 to ``k_audio_fade_max_ms``.
    [[nodiscard]] nlohmann::json to_json() const;
    /// Each field ``j`` holds, clamped; 0 for the rest, and all 0 when it is
    /// not an object.
    [[nodiscard]] static AudioFades from_json(const nlohmann::json& j);
};

/// The fields ``j`` sets (a service's or an audio file's): only fade_in_ms,
/// fade_out_ms and overlap_ms that are numbers, each clamped, and
/// on_play_pause if it is true or false. An empty object sets none, and so
/// does anything that is not an object.
[[nodiscard]] nlohmann::json set_audio_fades(const nlohmann::json& j);

/// ``under`` with each field ``set`` sets in its place.
[[nodiscard]] AudioFades override_audio_fades(AudioFades under, const nlohmann::json& set);

/// An audio item's fades on an output whose own are ``output``, under the
/// service's and the file's fields, each tier over those after it in
/// ``priority`` (``model::normalize_theme_priority``'s tiers: "content" is the
/// file, "service" the service, "global" the output; the default order when
/// empty). The output sets every field, so tiers after it go unheard.
[[nodiscard]] AudioFades resolve_audio_fades(const AudioFades& output,
                                             const nlohmann::json& service,
                                             const nlohmann::json& file,
                                             std::span<const std::string> priority = {});

} // namespace seventhslide::model
