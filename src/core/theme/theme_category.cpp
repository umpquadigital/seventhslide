#include "seventhslide/theme/theme_category.hpp"

namespace seventhslide::theme {

std::string_view to_string(ThemeCategory category) noexcept
{
    switch (category) {
    case ThemeCategory::Song:
        return "song";
    case ThemeCategory::Bible:
        return "bible";
    case ThemeCategory::Media:
        return "media";
    }
    return {};
}

std::optional<ThemeCategory> theme_category_from_string(std::string_view name) noexcept
{
    for (const auto category : k_theme_categories) {
        if (name == to_string(category)) {
            return category;
        }
    }
    return std::nullopt;
}

}  // namespace seventhslide::theme
