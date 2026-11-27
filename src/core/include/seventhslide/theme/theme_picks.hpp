#pragma once

// The themes one tier picks: an output's category defaults, and for each
// output the open service's, a rundown item's, a song's theme chip. Stored as
// JSON, per category:
//
//   {"song": {"theme": id, "bg": id}, "bible": {...}, "media": {"theme": id}}
//
// and read here, once, into the types the resolver walks, so the names are
// spelled in one place.
//
// Seam: plain C++20 + nlohmann/json.

#include "seventhslide/theme/theme_category.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::theme {

/// Which of a pick's two themes: the text theme or the background theme.
enum class ThemeField {
    Theme,
    Bg,
};

/// ``theme`` | ``bg``: the field's name in stored JSON.
[[nodiscard]] std::string_view to_string(ThemeField field) noexcept;

/// The field ``name`` spells, if any.
[[nodiscard]] std::optional<ThemeField> theme_field_from_string(std::string_view name) noexcept;

/**
 * The text and background theme one tier picks for one category, by library id.
 *
 * An empty id leaves that theme to the next tier.
 */
struct ThemePick {
    std::string theme;
    std::string bg;

    [[nodiscard]] const std::string& id(ThemeField field) const noexcept;
    [[nodiscard]] std::string& id(ThemeField field) noexcept;
    [[nodiscard]] bool empty() const noexcept { return theme.empty() && bg.empty(); }

    [[nodiscard]] bool operator==(const ThemePick&) const noexcept = default;
};

/**
 * One tier's picks for one output (or an output's own defaults): a pick per
 * category.
 *
 * Holds nothing for a category it was not allowed when read, and never a
 * background for one that takes none (``takes_background``).
 */
class ThemePicks {
public:
    ThemePicks() = default;

    /// ``j``'s picks for the ``allowed`` categories, ids trimmed. Anything not
    /// a non-blank string is unset, as is everything when ``j`` is not an object.
    [[nodiscard]] static ThemePicks from_json(const nlohmann::json& j, CategorySet allowed);
    /// The categories with a pick, and in each only the ids set.
    [[nodiscard]] nlohmann::json to_json() const;

    [[nodiscard]] const ThemePick& of(ThemeCategory category) const noexcept;
    /// Set (or, with an empty id, clear) one id. False, and nothing changed,
    /// for a background of a category that takes none.
    bool set(ThemeCategory category, ThemeField field, std::string id);

    [[nodiscard]] bool empty() const noexcept;

    [[nodiscard]] bool operator==(const ThemePicks&) const = default;

private:
    std::array<ThemePick, k_theme_category_count> picks_;
};

/**
 * One tier's picks for every output, keyed by ``OutputConfig::id``.
 *
 * Holds no empty entry: an output the tier leaves alone has none.
 */
class OutputThemeMap {
public:
    OutputThemeMap() = default;

    /// ``j``'s per-output picks for the ``allowed`` categories
    /// (``ThemePicks::from_json``). An entry that is not an object, or picks
    /// nothing, is dropped; so is everything when ``j`` is not an object.
    [[nodiscard]] static OutputThemeMap from_json(const nlohmann::json& j, CategorySet allowed);
    [[nodiscard]] nlohmann::json to_json() const;

    /// What the tier picks for ``output_id``; empty when it picks nothing.
    [[nodiscard]] const ThemePicks& picks(std::string_view output_id) const;
    [[nodiscard]] bool empty() const noexcept { return picks_.empty(); }

    [[nodiscard]] bool operator==(const OutputThemeMap&) const = default;

private:
    std::map<std::string, ThemePicks, std::less<>> picks_;
};

/// A service's picks, as its ``theme_map`` stores them: any category.
[[nodiscard]] OutputThemeMap service_themes_from_json(const nlohmann::json& j);

/// One choice a tier can make: one category's theme, or its background.
struct PickSlot {
    ThemeCategory category;
    ThemeField field;

    [[nodiscard]] bool operator==(const PickSlot&) const noexcept = default;
};

/// Every choice a tier picking for ``categories`` can make, by category in
/// ``k_theme_categories`` order: each one's theme, then its background where
/// it takes one.
[[nodiscard]] std::vector<PickSlot> pick_slots(CategorySet categories);

/// The choices content of ``kind`` takes from its own picks: its
/// ``theme_needs``. Nothing for content that needs nothing.
[[nodiscard]] std::vector<PickSlot> pick_slots(model::ContentKind kind);

}  // namespace seventhslide::theme
