#pragma once

// The content families a theme is chosen for (songs, Bible passages, media),
// and which of them each kind of content takes its looks from. Every tier of
// theme choice (an output's defaults, a service, a song's theme chip, a
// rundown item) picks per family, and the resolver asks this table which
// family's pick a slide uses.
//
// Seam: plain C++20.

#include "seventhslide/model/content_kind.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace seventhslide::theme {

/// A content family with looks of its own at every tier.
enum class ThemeCategory {
    Song,
    Bible,
    Media,
};

inline constexpr ThemeCategory k_theme_categories[] = {
    ThemeCategory::Song,
    ThemeCategory::Bible,
    ThemeCategory::Media,
};
inline constexpr std::size_t k_theme_category_count = std::size(k_theme_categories);

/// ``song`` | ``bible`` | ``media``: the category's name in stored JSON.
[[nodiscard]] std::string_view to_string(ThemeCategory category) noexcept;
[[nodiscard]] std::optional<ThemeCategory> theme_category_from_string(
    std::string_view name) noexcept;

/// Whether the category is given a background theme. Media is not: a
/// picture, video, web page or live source fills the canvas over black, and
/// audio paints nothing.
[[nodiscard]] constexpr bool takes_background(ThemeCategory category) noexcept
{
    return category != ThemeCategory::Media;
}

/// Whose picks content of one kind uses: the category its text theme comes
/// from, and the one its background comes from. Either may be none.
struct ThemeNeeds {
    std::optional<ThemeCategory> theme;
    std::optional<ThemeCategory> bg;

    [[nodiscard]] bool operator==(const ThemeNeeds&) const noexcept = default;
};

/**
 * What ``kind`` takes its looks from.
 *
 * A divider paints only a background, the songs' (it is a heading in the
 * rundown, shown between songs). A Freeform slide carries its own design and
 * background, so it takes nothing.
 */
[[nodiscard]] constexpr ThemeNeeds theme_needs(model::ContentKind kind) noexcept
{
    switch (kind) {
    case model::ContentKind::Song:
        return {ThemeCategory::Song, ThemeCategory::Song};
    case model::ContentKind::Bible:
        return {ThemeCategory::Bible, ThemeCategory::Bible};
    case model::ContentKind::Image:
    case model::ContentKind::ImageFolder:
    case model::ContentKind::Video:
    case model::ContentKind::Audio:
    case model::ContentKind::Web:
    case model::ContentKind::Live:
        return {ThemeCategory::Media, std::nullopt};
    case model::ContentKind::Divider:
    case model::ContentKind::Empty:
        return {std::nullopt, ThemeCategory::Song};
    case model::ContentKind::Freeform:
        break;
    }
    return {};
}

/// A set of categories: which ones a theme map may hold picks for.
class CategorySet {
public:
    constexpr CategorySet() = default;

    [[nodiscard]] static constexpr CategorySet all() noexcept
    {
        CategorySet set;
        for (const auto category : k_theme_categories) {
            set.add(category);
        }
        return set;
    }

    constexpr CategorySet& add(ThemeCategory category) noexcept
    {
        bits_ |= bit(category);
        return *this;
    }
    [[nodiscard]] constexpr bool contains(ThemeCategory category) const noexcept
    {
        return (bits_ & bit(category)) != 0;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }

    [[nodiscard]] constexpr bool operator==(const CategorySet&) const noexcept = default;

private:
    [[nodiscard]] static constexpr unsigned bit(ThemeCategory category) noexcept
    {
        return 1U << static_cast<unsigned>(category);
    }

    unsigned bits_ = 0;
};

/// The categories content of ``kind`` takes picks from (its ``theme_needs``):
/// what a map stored for such content may hold.
[[nodiscard]] constexpr CategorySet categories_for(model::ContentKind kind) noexcept
{
    const auto needs = theme_needs(kind);
    CategorySet set;
    if (needs.theme) {
        set.add(*needs.theme);
    }
    if (needs.bg) {
        set.add(*needs.bg);
    }
    return set;
}

}  // namespace seventhslide::theme
