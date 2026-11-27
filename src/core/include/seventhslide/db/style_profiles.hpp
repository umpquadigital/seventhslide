#pragma once

// Style profiles: named sets of theme assignments kept in the library
// database. The blob transforms are pure; StyleProfileService switches the
// live tables and output defaults between profiles through the repositories.

#include "seventhslide/db/config_repository.hpp"
#include "seventhslide/db/song_repository.hpp"
#include "seventhslide/db/style_profile_repository.hpp"
#include "seventhslide/model/content.hpp"
#include "seventhslide/model/output_config.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::db {

/// app_settings key for the active style profile id.
inline constexpr std::string_view k_active_profile_setting = "active_profile_id";

/// Profile blob keys beside ``outputs`` / ``songs``.
inline constexpr std::string_view k_profile_key_theme_priority = "theme_priority";
inline constexpr std::string_view k_profile_key_song_verse_orders = "song_verse_orders";

/// Live state a profile owns, gathered once so snapshotting stays a pure transform.
struct LiveProfileState {
    std::vector<std::pair<std::int64_t, nlohmann::json>> song_theme_maps;
    std::vector<std::pair<std::int64_t, std::string>> song_verse_orders;
    std::vector<std::string> theme_priority;
};

/// Every profile key present with a well-formed value (defaults for missing ones).
[[nodiscard]] nlohmann::json normalize_profile_data(const nlohmann::json& data);

/// Capture live assignment stores into a profile ``data`` blob.
/// Only non-empty song theme_maps and verse orders are stored (compact;
/// empty ≡ follow default).
[[nodiscard]] nlohmann::json snapshot_live(const std::vector<model::OutputConfig>& outputs,
                                           const LiveProfileState& live);

/// Profile data with current output defaults and empty library overrides.
[[nodiscard]] nlohmann::json blank_profile_data(const nlohmann::json& live_snapshot);

/// Apply profile ``outputs`` onto live configs: keep-current-if-absent.
void apply_output_defaults(std::vector<model::OutputConfig>& outputs,
                           const nlohmann::json& profile_outputs);

/// set-or-empty deltas for library theme_maps. Snapshot keys are string ids.
[[nodiscard]] std::vector<std::pair<std::int64_t, nlohmann::json>> theme_map_deltas(
    const std::vector<std::pair<std::int64_t, nlohmann::json>>& current,
    const nlohmann::json& snapshot_by_str_id);

/// set-or-empty deltas for song verse orders. Snapshot keys are string ids.
[[nodiscard]] std::vector<std::pair<std::int64_t, std::string>> verse_order_deltas(
    const std::vector<std::pair<std::int64_t, std::string>>& current,
    const nlohmann::json& snapshot_by_str_id);

/// Theme priority stored in ``data``, normalized (the default when absent).
[[nodiscard]] std::vector<std::string> profile_theme_priority(const nlohmann::json& data);

/// Owns switching between named style profiles (full port of StyleProfileManager).
///
/// Live tables always mirror the *active* profile. Service / item snapshot
/// theme_maps are intentionally NOT part of a profile.
class StyleProfileService {
public:
    StyleProfileService(StyleProfileRepository& profiles,
                        SongRepository& songs,
                        ConfigRepository& config,
                        std::vector<model::OutputConfig>& outputs);

    [[nodiscard]] nlohmann::json snapshot_live() const;
    void apply_snapshot(const nlohmann::json& data);

    /// Live state the profile owns, read straight from the repositories.
    [[nodiscard]] LiveProfileState read_live_state() const;

    /// Write the live state into the active profile, when it differs from
    /// what the profile holds.
    void capture_active();
    /// Capture old active (if switching), apply target, persist active id + outputs.
    [[nodiscard]] bool activate(std::int64_t target_id);

    [[nodiscard]] std::int64_t create_blank(std::string_view name);
    [[nodiscard]] std::optional<std::int64_t> duplicate(std::int64_t source_id,
                                                        std::string_view name);

    /// Guarantee one valid active profile (create Default, or repair stale id).
    void ensure_seeded();

    /// Delete profile. Refuses last profile. If deleting active, activates another first.
    /// Returns false if not found or last profile.
    [[nodiscard]] bool remove(std::int64_t id);

    [[nodiscard]] std::optional<std::int64_t> active_profile_id() const;
    void set_active_profile_id(std::optional<std::int64_t> id);

private:
    void persist_active_and_outputs();
    /// Theme priority currently in ``app_settings`` (normalized).
    [[nodiscard]] std::vector<std::string> live_theme_priority() const;

    StyleProfileRepository& profiles_;
    SongRepository& songs_;
    ConfigRepository& config_;
    std::vector<model::OutputConfig>& outputs_;
};

} // namespace seventhslide::db
