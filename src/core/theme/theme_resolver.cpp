#include "seventhslide/theme/theme_resolver.hpp"

#include "seventhslide/json/read.hpp"
#include "seventhslide/theme/theme_library.hpp"
#include "seventhslide/util/find.hpp"

#include <optional>
#include <vector>

namespace seventhslide::theme {

ThemeIds resolve_theme_ids(const model::OutputConfig& oc, const ThemeSelection& sel)
{
    const auto needs = theme_needs(sel.kind);
    if (!needs.theme && !needs.bg) {
        return {};
    }
    const auto defaults = ThemePicks::from_json(oc.category_defaults, CategorySet::all());
    const auto& service = sel.service_themes.picks(oc.id);
    const auto& content = sel.content_themes.picks(oc.id);

    // normalize_theme_priority gives every tier once, each by a name it reads.
    std::vector<const ThemePicks*> tiers;
    for (const auto& name : model::normalize_theme_priority(sel.theme_priority)) {
        const auto tier = model::theme_tier_from_string(name);
        if (!tier) {
            continue;
        }
        switch (*tier) {
        case model::ThemeTier::Content:
            tiers.push_back(&content);
            break;
        case model::ThemeTier::Service:
            tiers.push_back(&service);
            break;
        case model::ThemeTier::Global:
            tiers.push_back(&defaults);
            break;
        }
    }
    const auto first = [&](const std::optional<ThemeCategory>& category,
                           ThemeField field) -> std::string {
        if (!category) {
            return {};
        }
        for (const auto* tier : tiers) {
            if (const auto& id = tier->of(*category).id(field); !id.empty()) {
                return id;
            }
        }
        return {};
    };
    return {.theme_id = first(needs.theme, ThemeField::Theme),
            .bg_id = first(needs.bg, ThemeField::Bg)};
}

const model::ThemeEntry* find_theme(const std::vector<model::ThemeEntry>& themes,
                                    std::string_view theme_id)
{
    return theme_id.empty() ? nullptr : util::find_by(themes, theme_id, &model::ThemeEntry::id);
}

const model::ThemeEntry* resolve_text_theme(const model::OutputConfig& oc,
                                            const ThemeSelection& sel)
{
    const auto needs = theme_needs(sel.kind);
    if (!needs.theme) {
        return nullptr;
    }
    return find_theme(theme_list(oc, text_kind_of(*needs.theme)),
                      resolve_theme_ids(oc, sel).theme_id);
}

} // namespace seventhslide::theme
