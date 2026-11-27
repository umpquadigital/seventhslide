#pragma once

// Where each library's Audio Mixer is left: every stack's and output's level,
// mute and Listen.
//
// Levels suit this room's sound system, as destinations suit its screens, so
// they live beside the destinations in the app data dir rather than in the
// library: a library folder copied to another machine does not bring this
// one's levels along. Each library's strips are keyed by stack and output id,
// which survive a rename.
//
// Seam: plain C++20 + std::filesystem + nlohmann/json (no Qt).

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::library {

/// ``audio_mixer.json`` inside the app data dir.
inline constexpr std::string_view k_audio_mix_filename = "audio_mixer.json";

/// One strip of the mixer. The defaults are a strip never touched: 0 dB,
/// unmuted, not on Listen.
struct ChannelMix {
    /// At most 0; -infinity is the fader at its bottom (see audio_levels.hpp).
    double gain_db = 0.0;
    bool muted = false;
    bool listen = false;

    [[nodiscard]] bool operator==(const ChannelMix&) const = default;

    /// ``{"gain_db": -6.0, "muted": false, "listen": true}``; silence is
    /// ``"-inf"``.
    [[nodiscard]] nlohmann::json to_json() const;
    /// What ``j`` says that makes sense, defaults for the rest. The level is
    /// normalized (``media::normalize_gain_db``).
    static ChannelMix from_json(const nlohmann::json& j);
};

/// One library's mixer: a strip per stack and per output. A strip it does not
/// hold is at the defaults.
struct LibraryMix {
    std::map<std::string, ChannelMix, std::less<>> stacks;
    std::map<std::string, ChannelMix, std::less<>> outputs;

    [[nodiscard]] bool operator==(const LibraryMix&) const = default;

    [[nodiscard]] ChannelMix stack(std::string_view id) const;
    [[nodiscard]] ChannelMix output(std::string_view id) const;

    /// ``{"stacks": {id: strip}, "outputs": {id: strip}}``, leaving out the
    /// strips at the defaults.
    [[nodiscard]] nlohmann::json to_json() const;
    static LibraryMix from_json(const nlohmann::json& j);

    /// A library's mixer the first time it opens on this computer: the first
    /// of ``output_ids`` (the Outputs panel's order) on Listen, so the
    /// speakers play what the main output carries; everything else at the
    /// defaults.
    static LibraryMix first_use(const std::vector<std::string>& output_ids);
};

class AudioMixStore {
public:
    explicit AudioMixStore(std::filesystem::path data_dir);

    [[nodiscard]] std::filesystem::path file() const;

    /// ``library_id``'s mixer, or nullopt when this computer has never kept
    /// one for it (then ``LibraryMix::first_use`` applies). A missing or
    /// unreadable file reads as none kept.
    [[nodiscard]] std::optional<LibraryMix> load(std::string_view library_id) const;

    /// Replace ``library_id``'s mixer. The library stays known even when every
    /// strip is at the defaults, so first use is not applied to it again.
    /// Other libraries keep theirs. False on I/O failure.
    bool save(std::string_view library_id, const LibraryMix& mix) const;

    /// Forget a deleted stack's or output's strip. Nothing is written for a
    /// library this computer has kept no mixer for.
    bool remove_stack(std::string_view library_id, std::string_view stack_id) const;
    bool remove_output(std::string_view library_id, std::string_view output_id) const;

    /// Forget a library removed from the registry, so a later library given the
    /// same id starts as new.
    bool remove_library(std::string_view library_id) const;


private:
    [[nodiscard]] nlohmann::json read() const;
    [[nodiscard]] bool write(const nlohmann::json& doc) const;

    std::filesystem::path data_dir_;
};

}  // namespace seventhslide::library
