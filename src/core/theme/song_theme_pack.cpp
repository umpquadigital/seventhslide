#include "seventhslide/theme/song_theme_pack.hpp"

#include "seventhslide/json/read.hpp"
#include "seventhslide/parse/lyrics_blocks.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>

namespace seventhslide::theme {

using seventhslide::json::str_or;

ChipMaps chip_maps_from_json(const nlohmann::json& j)
{
    ChipMaps chips;
    if (!j.is_object()) {
        return chips;
    }
    for (auto it = j.begin(); it != j.end(); ++it) {
        auto chip_id = canonical_theme_chip_id(it.key());
        if (!chip_id.empty()) {
            chips.insert_or_assign(std::move(chip_id),
                                   OutputThemeMap::from_json(
                                       it.value(), categories_for(model::ContentKind::Song),
                                       AboveMarks::Keep));
        }
    }
    return chips;
}

std::vector<ThemeRun> theme_runs(const ChipMaps& chips, std::string_view verse_order)
{
    std::vector<ThemeRun> out;
    OutputThemeMap played;
    for (auto& segment : split_order_by_theme_chips(verse_order)) {
        if (!segment.chip_id.empty()) {
            const auto it = chips.find(segment.chip_id);
            played = (it != chips.end() ? it->second : OutputThemeMap{}).following(played);
        }
        out.push_back({.segment = std::move(segment), .picks = played});
    }
    return out;
}

OutputThemeMap previous_chip_picks(const ChipMaps& chips,
                                   std::string_view verse_order,
                                   std::size_t place)
{
    std::string before;
    std::size_t index = 0;
    for (const auto token : util::split_ws(verse_order)) {
        if (index++ == place) {
            break;
        }
        before.append(token).push_back(' ');
    }
    auto runs = theme_runs(chips, before);
    return runs.empty() ? OutputThemeMap{} : std::move(runs.back().picks);
}

SongThemeMap SongThemeMap::from_json(const nlohmann::json& j)
{
    SongThemeMap map;
    const auto* raw = seventhslide::json::find(j, "arrangements");
    if (raw == nullptr || !raw->is_array()) {
        return map;
    }
    const auto active = str_or(j, "active");
    for (const auto& a : *raw) {
        SongArrangement arr;
        arr.id = str_or(a, "id");
        if (arr.id.empty()) {
            continue;
        }
        arr.name = str_or(a, "name");
        arr.order = str_or(a, "order");
        arr.chips = chip_maps_from_json(seventhslide::json::object_or_empty(a, "chips"));
        if (arr.id == active) {
            map.active_ = active;
        }
        map.arrangements_.push_back(std::move(arr));
    }
    if (map.active_.empty() && !map.arrangements_.empty()) {
        map.active_ = map.arrangements_.front().id;
    }
    return map;
}

nlohmann::json SongThemeMap::to_json() const
{
    if (empty()) {
        return nlohmann::json::object();
    }
    nlohmann::json arrs = nlohmann::json::array();
    for (const auto& a : arrangements_) {
        nlohmann::json chips = nlohmann::json::object();
        for (const auto& [chip_id, picks] : a.chips) {
            chips[chip_id] = picks.to_json();
        }
        arrs.push_back({{"id", a.id},
                        {"name", a.name},
                        {"order", a.order},
                        {"chips", std::move(chips)}});
    }
    return {{"active", active_}, {"arrangements", std::move(arrs)}};
}

OutputThemeMap SongThemeMap::stored_chip(std::string_view chip_id) const
{
    const auto canonical = canonical_theme_chip_id(chip_id);
    if (canonical.empty()) {
        return {};
    }
    // ``active_`` names an arrangement whenever there is one (from_json).
    const auto arr = std::ranges::find(arrangements_, active_, &SongArrangement::id);
    if (arr == arrangements_.end()) {
        return {};
    }
    const auto it = arr->chips.find(canonical);
    return it != arr->chips.end() ? it->second : OutputThemeMap{};
}

std::vector<ThemeRun> SongThemeMap::runs(std::string_view verse_order) const
{
    // ``active_`` names an arrangement whenever there is one (from_json).
    const auto arr = std::ranges::find(arrangements_, active_, &SongArrangement::id);
    static const ChipMaps k_none;
    return theme_runs(arr != arrangements_.end() ? arr->chips : k_none, verse_order);
}

SongThemeMap pack_song_theme_map(const nlohmann::json& arrangements, std::string_view active)
{
    const auto& list = arrangements.is_array() ? arrangements : nlohmann::json::array();
    const auto current = std::find_if(list.begin(), list.end(), [&](const nlohmann::json& a) {
        return str_or(a, "id") == active;
    });
    const bool chips_in_order =
        current != list.end() && verse_order_has_theme_chips(str_or(*current, "order"));
    const auto first_name = list.empty() ? std::string{} : str_or(list.front(), "name");
    const bool only_default =
        list.size() <= 1 && (first_name.empty() || first_name == k_default_arrangement_name);
    if (only_default && !chips_in_order) {
        return {};
    }

    nlohmann::json arrs = nlohmann::json::array();
    for (std::size_t i = 0; i < list.size(); ++i) {
        auto id = str_or(list[i], "id");
        auto name = str_or(list[i], "name");
        arrs.push_back({{"id", id.empty() ? "a" + std::to_string(i + 1) : std::move(id)},
                        {"name", name.empty() ? std::string(k_default_arrangement_name)
                                              : std::move(name)},
                        {"order", str_or(list[i], "order")},
                        {"chips", seventhslide::json::object_or_empty(list[i], "chips")}});
    }
    std::string chosen(active);
    if (chosen.empty() && !arrs.empty()) {
        chosen = arrs.front().at("id").get<std::string>();
    }
    return SongThemeMap::from_json({{"active", chosen}, {"arrangements", std::move(arrs)}});
}

std::string canonical_theme_chip_id(std::string_view token)
{
    const auto lower = util::to_lower(token);
    if (!parse::is_theme_code(lower)) {
        return {};
    }
    return util::to_upper(lower);
}

bool verse_order_has_theme_chips(std::string_view verse_order) noexcept
{
    for (auto tok : util::split_ws(verse_order)) {
        if (parse::is_theme_code(util::to_lower(tok))) {
            return true;
        }
    }
    return false;
}

std::vector<ThemeChipSegment> split_order_by_theme_chips(std::string_view verse_order)
{
    std::vector<ThemeChipSegment> out;
    ThemeChipSegment cur;

    for (auto tok : util::split_ws(verse_order)) {
        const auto lower = util::to_lower(tok);
        if (parse::is_theme_code(lower)) {
            if (!cur.verse_order.empty() || !cur.chip_id.empty()) {
                out.push_back(cur);
            }
            cur = ThemeChipSegment{};
            cur.chip_id = canonical_theme_chip_id(lower);
            continue;
        }
        if (!cur.verse_order.empty()) {
            cur.verse_order.push_back(' ');
        }
        cur.verse_order.append(lower);
    }
    if (!cur.verse_order.empty() || !cur.chip_id.empty()) {
        out.push_back(std::move(cur));
    }
    return out;
}

}  // namespace seventhslide::theme
