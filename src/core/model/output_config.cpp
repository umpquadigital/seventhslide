#include <seventhslide/model/output_config.hpp>

#include "seventhslide/json/read.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>

namespace seventhslide::model {

std::string normalize_output_icon(std::string_view icon)
{
    if (std::ranges::find(k_output_icons, icon) != std::end(k_output_icons)) {
        return std::string(icon);
    }
    return std::string(k_default_output_icon);
}

namespace {

template <typename T>
void assign_if_typed(T& field, const nlohmann::json& v)
{
    if constexpr (std::is_same_v<T, int>) {
        if (v.is_number_integer()) {
            field = v.get<int>();
        } else if (v.is_number_float()) {
            field = static_cast<int>(v.get<double>());
        }
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (v.is_string()) {
            field = v.get<std::string>();
        }
    } else {
        static_assert(std::is_same_v<T, nlohmann::json>);
        if (v.is_object()) {
            field = v;
        }
    }
}

/// The theme entries in ``d[key]``, skipping anything not an object; empty
/// when it is not there or not a list.
[[nodiscard]] std::vector<ThemeEntry> themes_in(const nlohmann::json& d, const char* key)
{
    std::vector<ThemeEntry> out;
    const auto* v = json::find(d, key);
    if (v == nullptr || !v->is_array()) {
        return out;
    }
    for (const auto& el : *v) {
        if (el.is_object()) {
            out.push_back(el.get<ThemeEntry>());
        }
    }
    return out;
}

/// ``field`` from ``d[key]`` when it is there and of ``field``'s type.
template <typename T>
void read_field(const nlohmann::json& d, const char* key, T& field)
{
    if (const auto it = d.find(key); it != d.end()) {
        assign_if_typed(field, *it);
    }
}

} // namespace

void to_json(nlohmann::json& j, const ThemeEntry& t)
{
    j = nlohmann::json::object();
    j["id"] = t.id;
    j["name"] = t.name;
    j["style"] = t.style.is_null() ? nlohmann::json::object() : t.style;
    if (!t.tags.empty()) {
        j["tags"] = t.tags;
    }
    if (!t.created_at.empty()) {
        j["created_at"] = t.created_at;
    }
}

void from_json(const nlohmann::json& j, ThemeEntry& t)
{
    t = ThemeEntry{};
    if (!j.is_object()) {
        return;
    }
    t.id = json::str_or(j, "id");
    t.name = json::str_or(j, "name");
    t.style = json::object_or_empty(j, "style");
    t.tags = json::strings_or_empty(j, "tags");
    t.created_at = json::str_or(j, "created_at");
}

nlohmann::json OutputConfig::to_persist_dict() const
{
    nlohmann::json out = nlohmann::json::object();
    out["id"] = id;
    out["name"] = name;
    out["canvas_width"] = canvas_width;
    out["canvas_height"] = canvas_height;
    out["exempt_from_global_blank"] = exempt_from_global_blank;
    out["exempt_from_global_freeze"] = exempt_from_global_freeze;
    out["icon"] = icon;
    out[std::string(k_audio_fades_key)] = audio_fades.to_json();
    out["song_themes"] = song_themes;
    out["bible_themes"] = bible_themes;
    out["media_themes"] = media_themes;
    out["bg_themes"] = bg_themes;
    out["category_defaults"] = category_defaults;
    out["enabled_stack_ids"] = enabled_stack_ids;
    out["disabled_content_types"] = disabled_content_types;
    return out;
}

OutputConfig OutputConfig::from_dict(const nlohmann::json& d)
{
    OutputConfig oc;
    if (!d.is_object()) {
        return oc;
    }
    read_field(d, "id", oc.id);
    read_field(d, "name", oc.name);
    read_field(d, "canvas_width", oc.canvas_width);
    read_field(d, "canvas_height", oc.canvas_height);
    oc.exempt_from_global_blank = json::bool_or(d, "exempt_from_global_blank", false);
    oc.exempt_from_global_freeze = json::bool_or(d, "exempt_from_global_freeze", false);
    oc.icon = normalize_output_icon(json::str_or(d, "icon", k_default_output_icon));
    if (const auto it = d.find(std::string(k_audio_fades_key)); it != d.end()) {
        oc.audio_fades = AudioFades::from_json(*it);
    }
    oc.song_themes = themes_in(d, "song_themes");
    oc.bible_themes = themes_in(d, "bible_themes");
    oc.media_themes = themes_in(d, "media_themes");
    oc.bg_themes = themes_in(d, "bg_themes");
    read_field(d, "category_defaults", oc.category_defaults);
    oc.enabled_stack_ids = json::strings_or_empty(d, "enabled_stack_ids");
    oc.disabled_content_types = json::strings_or_empty(d, "disabled_content_types");
    return oc;
}

} // namespace seventhslide::model
