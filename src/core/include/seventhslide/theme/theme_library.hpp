#pragma once

// Theme library CRUD - the song, Bible, media and background ThemeEntry lists
// on OutputConfig.
// Plain C++20 + nlohmann/json; filesystem only for owned background assets.

#include <seventhslide/model/output_config.hpp>
#include <seventhslide/theme/theme_kind.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::theme {

[[nodiscard]] std::string new_theme_id();

inline constexpr std::size_t k_max_theme_tags = 24;
/// Longest tag kept, in bytes (cut back to a UTF-8 character boundary).
inline constexpr std::size_t k_max_theme_tag_bytes = 40;

/// The one tag rule, for storage and for display: trimmed, runs of whitespace
/// collapsed to one space, lowercased (ASCII), capped at
/// ``k_max_theme_tag_bytes``, deduplicated, and at most ``k_max_theme_tags``.
/// ``raw`` is a comma-separated string or an array of strings.
[[nodiscard]] std::vector<std::string> normalize_theme_tags(const nlohmann::json& raw);

/// ``per_context`` (a text theme's ``elements`` or ``transitions``: one entry
/// per design context) with only the entries of ``kind``'s contexts
/// (``contexts_of``), and the element set's own fields (``canvas``,
/// ``next_id``). Anything else, a context no build has any more included,
/// goes.
[[nodiscard]] nlohmann::json own_contexts_only(ThemeKind kind, const nlohmann::json& per_context);

/**
 * ``style_in`` as a theme of ``kind`` stores it: only that kind's settings
 * (``is_theme_key``), and of a text theme's ``elements`` and ``transitions``
 * only its own contexts' (``contexts_of``). The one gate every theme written
 * goes through, so a stored theme never holds another kind's design.
 */
[[nodiscard]] nlohmann::json filter_theme_style(ThemeKind kind, const nlohmann::json& style_in);

/// ``category_defaults`` naming, for every category, the first theme of its
/// kind, and the first background for every one that takes a background:
/// what a new output starts with.
[[nodiscard]] nlohmann::json first_category_defaults(const model::OutputConfig& oc);

/// Ensure >=1 theme of each kind, and category_defaults for every category. A
/// Base theme made here takes its id from the output's and its kind, so the
/// same output gets the same ids each time. Returns true when ``oc`` was
/// mutated.
bool seed_default_themes(model::OutputConfig& oc);

/// A new output as every output starts: a fresh id, ``name`` sanitised and made
/// unique among ``existing``, the reference canvas, and the Base themes.
[[nodiscard]] model::OutputConfig new_output(std::string_view name,
                                             const std::vector<model::OutputConfig>& existing);

[[nodiscard]] std::vector<model::ThemeEntry>& theme_list(model::OutputConfig& oc, ThemeKind kind);
[[nodiscard]] const std::vector<model::ThemeEntry>& theme_list(const model::OutputConfig& oc,
                                                               ThemeKind kind);

[[nodiscard]] model::ThemeEntry* find_theme(model::OutputConfig& oc,
                                            ThemeKind kind,
                                            std::string_view theme_id);
[[nodiscard]] const model::ThemeEntry* find_theme(const model::OutputConfig& oc,
                                                  ThemeKind kind,
                                                  std::string_view theme_id);

struct ThemeMutationResult {
    bool ok = false;
    std::string message;
    model::ThemeEntry theme;
};

/// Append a new theme. Optional ``style_in`` is filtered; empty -> snapshot from ``oc``.
/// A text ``style_in``'s elements are in ``oc``'s canvas pixels (the designer
/// edits on the output's canvas) and are stamped with that canvas.
/// ``duplicate_from_id`` copies style/tags/extras from an existing theme of the same kind.
/// Always stamps extras ``created_at`` (ISO local ``yyyy-MM-ddTHH:mm:ss``).
[[nodiscard]] ThemeMutationResult create_theme(model::OutputConfig& oc,
                                               ThemeKind kind,
                                               std::string_view name,
                                               const nlohmann::json& style_in = nlohmann::json::object(),
                                               const nlohmann::json& tags_in = nlohmann::json(),
                                               std::string_view duplicate_from_id = {});

/// Replace name and style; tags too when ``apply_tags``. As with ``create_theme``,
/// a text ``style_in``'s elements are in ``oc``'s canvas pixels.
[[nodiscard]] ThemeMutationResult update_theme(model::OutputConfig& oc,
                                               ThemeKind kind,
                                               std::string_view theme_id,
                                               std::string_view name,
                                               const nlohmann::json& style_in,
                                               const nlohmann::json& tags_in = nlohmann::json(),
                                               bool apply_tags = false);

/// Delete theme; blocks last-of-kind; rewires category_defaults. It deletes no
/// files: the caller deletes a background theme's (``delete_theme_owned_files``)
/// once the outputs without it are saved, so a failed save loses nothing.
[[nodiscard]] ThemeMutationResult delete_theme(model::OutputConfig& oc,
                                               ThemeKind kind,
                                               std::string_view theme_id);


/// Set one default: ``category`` a ThemeCategory's name, ``field`` a
/// ThemeField's (``theme`` names a text theme, ``bg`` a background theme).
/// A category that takes no background refuses one.
[[nodiscard]] ThemeMutationResult set_category_default(model::OutputConfig& oc,
                                                       std::string_view category,
                                                       std::string_view field,
                                                       std::string_view theme_id);

// --- Owned theme background / video assets under ``media_dir/theme_backgrounds`` ---
//
// A theme owns its background files: each one is named for the theme and slot,
// and is never changed once written (a new background is a new file). Copying
// a file in is the caller's job, so the app can do it off the GUI thread.
// These functions only name, link, point at and delete owned files, so none
// of them takes longer than a directory listing.

inline constexpr std::string_view k_theme_bg_url_prefix = "/static/theme_backgrounds/";

/// Where owned theme assets live under ``media_dir``.
[[nodiscard]] std::filesystem::path theme_assets_dir(const std::filesystem::path& media_dir);

/// ``bg`` (background image) or ``bg_video``.
[[nodiscard]] bool is_theme_asset_slot(std::string_view slot) noexcept;

/// The URL ``slot`` holds on ``theme``; empty for none or an unknown slot.
[[nodiscard]] std::string theme_asset_url(const model::ThemeEntry& theme, std::string_view slot);

/// A fresh owned file name for ``theme_id``'s ``slot``, ending in
/// ``extension`` (".png", say). Write the file there, then
/// ``set_owned_theme_asset``.
[[nodiscard]] std::string new_owned_asset_name(std::string_view theme_id,
                                               std::string_view slot,
                                               std::string_view extension);

/// Point ``slot`` at ``filename``, an owned file already in
/// ``theme_assets_dir``. It deletes no files: the caller deletes the slot's
/// others (``prune_unused_theme_assets``) once the outputs naming the new one
/// are saved, so a failure never loses the background.
[[nodiscard]] ThemeMutationResult set_owned_theme_asset(model::ThemeEntry& theme,
                                                        std::string_view slot,
                                                        std::string_view filename);

/// Delete the files ``theme`` owns that its style no longer names: copies a
/// draft made and did not keep, or what a saved change replaced. Only
/// ``slot``'s, when it is given.
void prune_unused_theme_assets(const model::ThemeEntry& theme,
                               const std::filesystem::path& media_dir,
                               std::string_view slot = {});

/// Delete all owned files for a theme id (on theme delete).
void delete_theme_owned_files(std::string_view theme_id,
                              const std::filesystem::path& media_dir,
                              std::string_view slot = {});

/// A background a theme still shares with another theme, waiting for its own
/// copy (see ``sync_theme_owned_assets``).
struct PendingAssetCopy {
    std::string slot;
    /// The shared URL the slot holds now. The copy replaces it only if the
    /// slot still holds it when the copy is done.
    std::string url;
    std::filesystem::path source;
};

struct AssetSyncResult {
    /// The theme's style changed.
    bool changed = false;
    std::vector<PendingAssetCopy> copies;
};

/// Enforce ownership: a slot pointing at another theme's file (a duplicated
/// theme, say) gets a file of its own, so deleting either theme cannot break
/// the other. It deletes no files: the caller deletes those the theme no
/// longer names (``prune_unused_theme_assets``) once the outputs are saved,
/// so a failure never loses the background.
///
/// Owned files never change, so the new file is a hard link: instant, and as
/// independent as a copy. Where the filesystem has no hard links the slot is
/// left on the shared URL and listed in ``copies``; the caller copies it off
/// the GUI thread and finishes with ``set_owned_theme_asset``.
[[nodiscard]] AssetSyncResult sync_theme_owned_assets(model::ThemeEntry& theme,
                                                      const std::filesystem::path& media_dir);

/// The backgrounds ``theme`` shares with another theme whose files are not
/// here, by path under the media folder ("theme_backgrounds/<name>"):
/// ``sync_theme_owned_assets`` cannot give it copies of its own, and leaves it
/// pointing at the other theme's.
[[nodiscard]] std::vector<std::string> shared_assets_not_here(const model::ThemeEntry& theme,
                                                              const std::filesystem::path& media_dir);

} // namespace seventhslide::theme
