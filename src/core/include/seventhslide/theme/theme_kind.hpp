#pragma once

// What each kind of theme is. An output keeps a list of each: Song themes,
// Bible themes and Media themes (each the text theme of its category) and
// background themes. Everything that asks what belongs to a kind - which
// design contexts, which settings, what a thumbnail shows - asks here.
//
// Seam: plain C++20.

#include "seventhslide/theme/theme_category.hpp"
#include "seventhslide/theme/theme_elements.hpp"

#include <optional>
#include <span>
#include <string_view>

namespace seventhslide::theme {

enum class ThemeKind {
    Song,
    Bible,
    Media,
    Bg,
};

inline constexpr ThemeKind k_theme_kinds[] = {
    ThemeKind::Song,
    ThemeKind::Bible,
    ThemeKind::Media,
    ThemeKind::Bg,
};

/// ``song`` | ``bible`` | ``media`` | ``bg``: what the app and QML call it.
[[nodiscard]] std::string_view to_string(ThemeKind kind) noexcept;
[[nodiscard]] std::optional<ThemeKind> parse_theme_kind(std::string_view raw) noexcept;

/// The kind of text theme ``category`` takes.
[[nodiscard]] constexpr ThemeKind text_kind_of(ThemeCategory category) noexcept
{
    switch (category) {
    case ThemeCategory::Song:
        return ThemeKind::Song;
    case ThemeCategory::Bible:
        return ThemeKind::Bible;
    case ThemeCategory::Media:
        return ThemeKind::Media;
    }
    return ThemeKind::Song;
}

/// The category a text theme of ``kind`` serves; none for a background.
[[nodiscard]] constexpr std::optional<ThemeCategory> category_of(ThemeKind kind) noexcept
{
    switch (kind) {
    case ThemeKind::Song:
        return ThemeCategory::Song;
    case ThemeKind::Bible:
        return ThemeCategory::Bible;
    case ThemeKind::Media:
        return ThemeCategory::Media;
    case ThemeKind::Bg:
        break;
    }
    return std::nullopt;
}

/// The design contexts a theme of ``kind`` holds, in the designer's order;
/// none for a background. Every context is one kind's.
[[nodiscard]] std::span<const ElementContext> contexts_of(ThemeKind kind) noexcept;

/// The kind whose themes hold ``ctx``.
[[nodiscard]] ThemeKind kind_of(ElementContext ctx) noexcept;

/// The context a theme of ``kind`` is shown in first: in its thumbnail, and
/// when the designer opens it. ``kind`` is a text kind.
[[nodiscard]] ElementContext primary_context(ThemeKind kind) noexcept;

/// Whether ``key`` is one of the settings a theme of ``kind`` stores.
[[nodiscard]] bool is_theme_key(ThemeKind kind, std::string_view key) noexcept;

}  // namespace seventhslide::theme
