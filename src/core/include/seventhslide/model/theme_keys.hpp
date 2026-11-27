#pragma once

// Output-config key tables. Every key array must stay sorted (``key_in``
// binary-searches them).

#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::model {

// Output settings / theme libraries (not themeable style).
inline constexpr std::string_view k_output_structural_keys[] = {
    "bg_themes",
    "bible_themes",
    "canvas_height",
    "canvas_width",
    "category_defaults",
    "disabled_content_types",
    "enabled_stack_ids",
    "id",
    "media_themes",
    "name",
    "song_themes",
};

// Background theme field set.
inline constexpr std::string_view k_bg_theme_keys[] = {
    "background_color",
    "background_image",
    "background_type",
    "background_video",
    "bg_enable_fade",
    "bg_fade_duration",
};

// Intrinsic output toggles (not themed).
inline constexpr std::string_view k_output_intrinsic_keys[] = {
    "exempt_from_global_blank",
    "exempt_from_global_freeze",
};

/**
 * Text theme field sets, one per kind of text theme.
 *
 * A text theme's design is ``elements`` (only its own design contexts). What
 * is left here is the handful of settings that are not attached to any one
 * object: packing policy and the content fade (``transitions``, only its own
 * contexts'). Geometry and typography live on the elements and are read from
 * ``theme::ResolvedTheme`` after resolve.
 */
inline constexpr std::string_view k_song_theme_keys[] = {
    "balance_wrapped_lines",
    "balance_wrapped_strength",
    "elements",
    "fluid_slides",
    "follow_lines",
    "prevent_mixed_active",
    "show_title_slides",
    "title_slide_show_lines",
    "transitions",
    "verse_gap",
};

inline constexpr std::string_view k_bible_theme_keys[] = {
    "bible_fluid_verses",
    "elements",
    "transitions",
};

inline constexpr std::string_view k_media_theme_keys[] = {
    "elements",
    "transitions",
};

/// The theme priority tiers, in their default order. A stored priority is any
/// permutation of these (see ``normalize_theme_priority``).
inline constexpr std::string_view k_default_theme_priority[] = {
    "content",
    "service",
    "global",
};

/// Whether the sorted table ``keys`` holds ``key``.
[[nodiscard]] inline bool key_in(std::string_view key,
                                 std::span<const std::string_view> keys) noexcept
{
    return std::binary_search(keys.begin(), keys.end(), key);
}

[[nodiscard]] inline bool is_bg_theme_key(std::string_view key) noexcept
{
    return key_in(key, k_bg_theme_keys);
}

/// Where a slide's theme can be chosen: on the item (or song), on the service,
/// or as the output's default for the category.
enum class ThemeTier {
    Content,
    Service,
    Global,
};

/// The tier ``name`` spells, as stored in a theme priority.
[[nodiscard]] constexpr std::optional<ThemeTier> theme_tier_from_string(
    std::string_view name) noexcept
{
    if (name == "content") {
        return ThemeTier::Content;
    }
    if (name == "service") {
        return ThemeTier::Service;
    }
    if (name == "global") {
        return ThemeTier::Global;
    }
    return std::nullopt;
}
static_assert(std::ranges::all_of(k_default_theme_priority, [](std::string_view name) {
    return theme_tier_from_string(name).has_value();
}));

/// ``order`` (each tier name trimmed) when it is a permutation of the theme
/// priority tiers, else the default order.
[[nodiscard]] inline std::vector<std::string> normalize_theme_priority(
    std::vector<std::string> order)
{
    for (auto& tier : order) {
        tier = util::trim(tier);
    }
    const auto* tiers_begin = std::begin(k_default_theme_priority);
    const auto* tiers_end = std::end(k_default_theme_priority);
    if (order.size() == std::size(k_default_theme_priority)
        && std::is_permutation(order.begin(), order.end(), tiers_begin, tiers_end)) {
        return order;
    }
    return {tiers_begin, tiers_end};
}

} // namespace seventhslide::model
