#include "seventhslide/theme/theme_picks.hpp"

#include "seventhslide/json/read.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <ranges>
#include <utility>

namespace seventhslide::theme {
namespace {

/// Each field and its name in stored JSON.
struct FieldName {
    ThemeField field;
    const char* name;
};
constexpr FieldName k_fields[] = {{ThemeField::Theme, "theme"}, {ThemeField::Bg, "bg"}};

[[nodiscard]] constexpr std::size_t index_of(ThemeCategory category) noexcept
{
    return static_cast<std::size_t>(category);
}

}  // namespace

std::string_view to_string(ThemeField field) noexcept
{
    for (const auto& f : k_fields) {
        if (f.field == field) {
            return f.name;
        }
    }
    return {};
}

std::optional<ThemeField> theme_field_from_string(std::string_view name) noexcept
{
    for (const auto& f : k_fields) {
        if (name == f.name) {
            return f.field;
        }
    }
    return std::nullopt;
}

const std::string& ThemePick::id(ThemeField field) const noexcept
{
    return field == ThemeField::Theme ? theme : bg;
}

std::string& ThemePick::id(ThemeField field) noexcept
{
    return field == ThemeField::Theme ? theme : bg;
}

ThemePicks ThemePicks::from_json(const nlohmann::json& j, CategorySet allowed, AboveMarks above)
{
    ThemePicks picks;
    for (const auto category : k_theme_categories) {
        if (!allowed.contains(category)) {
            continue;
        }
        const auto* raw = json::find(j, std::string(to_string(category)).c_str());
        if (raw == nullptr) {
            continue;
        }
        for (const auto& f : k_fields) {
            auto id = util::trim(json::str_or(*raw, f.name));
            if (above == AboveMarks::Drop && id == k_inherit_above) {
                continue;
            }
            (void)picks.set(category, f.field, std::move(id));
        }
    }
    return picks;
}

nlohmann::json ThemePicks::to_json() const
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto category : k_theme_categories) {
        const auto& pick = of(category);
        if (pick.empty()) {
            continue;
        }
        nlohmann::json entry = nlohmann::json::object();
        for (const auto& f : k_fields) {
            if (const auto& id = pick.id(f.field); !id.empty()) {
                entry[f.name] = id;
            }
        }
        out[std::string(to_string(category))] = std::move(entry);
    }
    return out;
}

const ThemePick& ThemePicks::of(ThemeCategory category) const noexcept
{
    return picks_[index_of(category)];
}

bool ThemePicks::set(ThemeCategory category, ThemeField field, std::string id)
{
    if (field == ThemeField::Bg && !takes_background(category)) {
        return false;
    }
    picks_[index_of(category)].id(field) = std::move(id);
    return true;
}

bool ThemePicks::empty() const noexcept
{
    return std::ranges::all_of(picks_, &ThemePick::empty);
}

ThemePicks ThemePicks::following(const ThemePicks& previous) const
{
    ThemePicks out = previous;
    for (const auto category : k_theme_categories) {
        for (const auto& f : k_fields) {
            const auto& id = of(category).id(f.field);
            if (id == k_inherit_above) {
                (void)out.set(category, f.field, {});
            } else if (!id.empty()) {
                (void)out.set(category, f.field, id);
            }
        }
    }
    return out;
}

OutputThemeMap OutputThemeMap::from_json(const nlohmann::json& j, CategorySet allowed,
                                         AboveMarks above)
{
    OutputThemeMap map;
    if (!j.is_object()) {
        return map;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        auto picks = ThemePicks::from_json(it.value(), allowed, above);
        if (!picks.empty()) {
            map.picks_.insert_or_assign(it.key(), std::move(picks));
        }
    }
    return map;
}

nlohmann::json OutputThemeMap::to_json() const
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [output_id, picks] : picks_) {
        out[output_id] = picks.to_json();
    }
    return out;
}

OutputThemeMap OutputThemeMap::following(const OutputThemeMap& previous) const
{
    OutputThemeMap out;
    const auto add = [&](const std::string& output_id) {
        if (out.picks_.contains(output_id)) {
            return;
        }
        auto played = picks(output_id).following(previous.picks(output_id));
        if (!played.empty()) {
            out.picks_.emplace(output_id, std::move(played));
        }
    };
    for (const auto& output_id : std::views::keys(picks_)) {
        add(output_id);
    }
    for (const auto& output_id : std::views::keys(previous.picks_)) {
        add(output_id);
    }
    return out;
}

const ThemePicks& OutputThemeMap::picks(std::string_view output_id) const
{
    static const ThemePicks k_none;
    if (output_id.empty()) {
        return k_none;
    }
    const auto it = picks_.find(output_id);
    return it != picks_.end() ? it->second : k_none;
}

OutputThemeMap service_themes_from_json(const nlohmann::json& j)
{
    return OutputThemeMap::from_json(j, CategorySet::all());
}

std::vector<PickSlot> pick_slots(CategorySet categories)
{
    std::vector<PickSlot> slots;
    for (const auto category : k_theme_categories) {
        if (!categories.contains(category)) {
            continue;
        }
        slots.push_back({category, ThemeField::Theme});
        if (takes_background(category)) {
            slots.push_back({category, ThemeField::Bg});
        }
    }
    return slots;
}

std::vector<PickSlot> pick_slots(model::ContentKind kind)
{
    const auto needs = theme_needs(kind);
    std::vector<PickSlot> slots;
    if (needs.theme) {
        slots.push_back({*needs.theme, ThemeField::Theme});
    }
    if (needs.bg) {
        slots.push_back({*needs.bg, ThemeField::Bg});
    }
    return slots;
}

}  // namespace seventhslide::theme
