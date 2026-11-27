#pragma once

// Text-theme fixtures shared by the slide, scene and session tests.

#include <seventhslide/model/output_config.hpp>
#include <seventhslide/devsupport/flat_style.hpp>
#include <seventhslide/theme/resolved_theme.hpp>
#include <seventhslide/theme/theme_elements.hpp>
#include <seventhslide/theme/theme_library.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::test {

/// A static text element for a theme's Titles context.
inline theme::Element title_text_element(std::string text = "{song-title}")
{
    theme::Element el;
    el.object.type = freeform::ObjectType::Text;
    el.object.x = 100;
    el.object.y = 200;
    el.object.w = 800;
    el.object.h = 120;
    el.object.font_family = "Helvetica";
    el.object.font_size = 48;
    el.object.font_color = "#ffffff";
    el.object.text_align = "center";
    el.object.vertical_align = "middle";
    el.object.bold = true;
    el.object.text = std::move(text);
    return el;
}

/// ``style`` carrying ``set``, with ``titles`` at the back of its Titles context
/// (below the title copyright and any title clock).
inline nlohmann::json with_titles(nlohmann::json style,
                                  theme::ElementSet set,
                                  std::vector<theme::Element> titles)
{
    const int shift = 10 * static_cast<int>(titles.size());
    for (auto& el : set.titles) {
        el.object.z += shift;
    }
    for (std::size_t i = 0; i < titles.size(); ++i) {
        auto& el = titles[i];
        if (el.object.id.empty()) {
            el.object.id = "te_title" + std::to_string(i + 1);
        }
        el.object.z = 10 * static_cast<int>(i);
    }
    set.titles.insert(set.titles.begin(), titles.begin(), titles.end());
    style[std::string(theme::k_elements_key)] = set.to_json();
    return style;
}

/// ``style`` with the design its flat body keys describe, plus ``titles``.
inline nlohmann::json style_with_titles(nlohmann::json style,
                                        std::vector<theme::Element> titles)
{
    auto set = devsupport::elements_from_flat_style(style);
    return with_titles(std::move(style), std::move(set), std::move(titles));
}

/// ``style`` as a text theme the app reads: when it has no element model, the
/// design its flat body keys describe is added as one. The keys stay, for the
/// behaviour and background settings that share their names.
inline nlohmann::json flat_style(nlohmann::json style)
{
    if (!style.is_object()) {
        style = nlohmann::json::object();
    }
    if (!theme::has_elements(style)) {
        style[std::string(theme::k_elements_key)] =
            devsupport::elements_from_flat_style(style).to_json();
    }
    return style;
}

/// A resolved look from flat body keys. Layout tests that used to fill an
/// ``OutputConfig`` and hand it to the packer use this instead.
inline theme::ResolvedTheme resolved_from_flat(const nlohmann::json& flat,
                                               int canvas_width = 1920,
                                               int canvas_height = 1080)
{
    const auto text = flat_style(flat);
    return theme::resolve_styles(text, text, {canvas_width, canvas_height});
}

/// The box fixed-size chrome gets: anchored top-left, wide enough for its text.
inline freeform::Object chrome_box(freeform::ObjectType type,
                                   double x,
                                   double y,
                                   int font_size,
                                   double width_per_pt)
{
    freeform::Object o;
    o.type = type;
    o.x = x;
    o.y = y;
    o.font_size = std::max(1, font_size);
    o.w = std::max(72.0, static_cast<double>(o.font_size) * width_per_pt);
    o.h = std::max(28.0, static_cast<double>(o.font_size) + 10.0);
    o.text_align = "left";
    o.vertical_align = "top";
    return o;
}

/// A wall clock. By context the ids are ``te_clock`` (song), ``te_bible_clock``,
/// ``te_media_clock`` and ``te_title_clock``.
inline theme::Element clock_element(std::string id,
                                    double x = 10,
                                    double y = 10,
                                    int z = 30,
                                    int font_size = 48)
{
    theme::Element el;
    el.object = chrome_box(freeform::ObjectType::Clock, x, y, font_size, 3.2);
    el.object.id = std::move(id);
    el.object.z = z;
    el.object.font_family = "Helvetica";
    el.object.bold = false;
    el.object.italic = false;
    el.object.font_color = "#ffffff";
    el.object.clock_24h = false;
    el.object.clock_seconds = false;
    return el;
}

/// The song verse indicator.
inline theme::Element indicator_element(double x = 10, double y = 1000, int font_size = 30)
{
    theme::Element el;
    el.object = chrome_box(freeform::ObjectType::Indicator, x, y, font_size, 2.5);
    el.object.id = "te_indicator";
    el.object.z = 10;
    el.binding = theme::ElementBinding::Indicator;
    el.object.font_family = "Helvetica";
    el.object.opacity = 1.0;
    return el;
}

/// The playing video's remaining-time countdown.
inline theme::Element countdown_element(double x = 10, double y = 50, int font_size = 30)
{
    theme::Element el;
    el.object = chrome_box(freeform::ObjectType::Clock, x, y, font_size, 3.0);
    el.object.id = "te_countdown";
    el.object.z = 20;
    el.binding = theme::ElementBinding::Countdown;
    el.object.font_family = "Helvetica";
    el.object.bold = false;
    el.object.italic = false;
    el.object.font_color = "#ffffff";
    el.object.text_align = "left";
    return el;
}

/// The element ``id`` in ``list``.
inline theme::Element& element_in(std::vector<theme::Element>& list, std::string_view id)
{
    for (auto& el : list) {
        if (el.object.id == id) {
            return el;
        }
    }
    throw std::logic_error("no element " + std::string(id));
}

/// Point the copyright notice - the song's and its Titles copy - at ``text``.
inline void set_copyright_text(theme::ElementSet& set, const std::string& text)
{
    element_in(set.song, "te_copyright").object.text = text;
    element_in(set.titles, "te_title_copyright").object.text = text;
}

/// Insert ``el`` in paint order (after everything at or below its z).
inline void add_element(std::vector<theme::Element>& list, theme::Element el)
{
    const auto at = std::upper_bound(
        list.begin(), list.end(), el.object.z,
        [](int z, const theme::Element& other) { return z < other.object.z; });
    list.insert(at, std::move(el));
}

/// The design ``flat`` describes, or the stock design when it is empty.
inline theme::ElementSet design_of(const nlohmann::json& flat = nlohmann::json::object())
{
    return devsupport::elements_from_flat_style(flat);
}

/// One of the text themes the look helpers below keep, and its kind.
struct LookTheme {
    theme::ThemeKind kind;
    model::ThemeEntry* entry;
};

/// The text themes the look helpers below keep, one per kind, each holding
/// its kind's contexts of the one design (as ``devsupport::install_flat_style``
/// installs them).
inline std::vector<LookTheme> look_themes(model::OutputConfig& oc)
{
    std::vector<LookTheme> out;
    for (const auto kind : theme::k_theme_kinds) {
        if (kind == theme::ThemeKind::Bg) {
            continue;
        }
        auto& themes = theme::theme_list(oc, kind);
        if (!themes.empty()) {
            out.push_back({kind, &themes.front()});
        }
    }
    return out;
}

/// ``set`` as a ``kind`` theme stores it: its own contexts only.
inline nlohmann::json design_json_for(theme::ThemeKind kind, const theme::ElementSet& set)
{
    return theme::own_contexts_only(kind, set.to_json());
}

/// The design already installed on ``oc``, put back together from each kind's
/// contexts of it, or the stock design when none is.
inline theme::ElementSet design_of(const model::OutputConfig& oc)
{
    std::optional<theme::ElementSet> design;
    for (const auto kind : theme::k_theme_kinds) {
        if (kind == theme::ThemeKind::Bg) {
            continue;
        }
        const auto& themes = theme::theme_list(oc, kind);
        if (themes.empty() || !theme::has_elements(themes.front().style)) {
            continue;
        }
        auto own = theme::ElementSet::from_json(theme::element_model(themes.front().style));
        if (!design) {
            design = own;
        }
        for (const auto ctx : theme::contexts_of(kind)) {
            design->at(ctx) = std::move(own.at(ctx));
        }
        design->next_id = std::max(design->next_id, own.next_id);
    }
    return design ? *std::move(design) : theme::default_elements();
}

/// Describe ``oc``'s look with the retired flat keys.
///
/// The first call installs a theme of each kind. A later call merges the keys
/// into each text theme and, when they include body geometry, rebuilds the
/// element model from the merged style. Element edits made with
/// ``use_design`` after this are kept until the next call that carries a body key.
inline void set_look(model::OutputConfig& oc, const nlohmann::json& flat)
{
    if (!flat.is_object()) {
        return;
    }
    if (look_themes(oc).empty()) {
        devsupport::install_flat_style(oc, flat);
        return;
    }
    for (const auto& look : look_themes(oc)) {
        auto& style = look.entry->style;
        const bool had_elements = theme::has_elements(style);
        for (auto it = flat.begin(); it != flat.end(); ++it) {
            style[it.key()] = it.value();
        }
        const bool body = flat.contains("font_size") || flat.contains("width_px")
                          || flat.contains("box_x") || flat.contains("bible_text_box_width")
                          || flat.contains("show_lyrics") || flat.contains("show_bible_text")
                          || flat.contains("bible_main_font_size") || flat.contains("show_chords");
        if (!had_elements || body) {
            style[std::string(theme::k_elements_key)] =
                design_json_for(look.kind, devsupport::elements_from_flat_style(style));
        }
    }
    if (!oc.bg_themes.empty()) {
        auto& bg = oc.bg_themes.front().style;
        for (const char* key : {"background_type", "background_color", "background_image",
                                "background_video", "bg_enable_fade", "bg_fade_duration"}) {
            if (flat.contains(key)) {
                bg[key] = flat.at(key);
            }
        }
    }
}

/// ``spec`` as every content type's transition, the way a text theme stores them.
inline nlohmann::json every_transition(const model::Transition& spec)
{
    nlohmann::json all = nlohmann::json::object();
    for (const auto ctx : theme::k_content_contexts) {
        all[std::string(theme::to_string(ctx))] = spec;
    }
    return all;
}

/// Paint ``set`` as ``oc``'s look: each kind's text theme, holding its kind's
/// contexts of it. Behaviour keys already on those themes stay.
inline void use_design(model::OutputConfig& oc, const theme::ElementSet& set)
{
    for (const auto kind : theme::k_theme_kinds) {
        auto& themes = theme::theme_list(oc, kind);
        if (themes.empty()) {
            model::ThemeEntry entry;
            entry.id = kind == theme::ThemeKind::Bg ? "design-bg" : "design";
            entry.name = "Design";
            entry.style = nlohmann::json::object();
            themes.push_back(std::move(entry));
        }
    }
    for (const auto& look : look_themes(oc)) {
        look.entry->style[std::string(theme::k_elements_key)] = design_json_for(look.kind, set);
    }
    if (!oc.category_defaults.is_object() || oc.category_defaults.empty()) {
        oc.category_defaults = theme::first_category_defaults(oc);
    }
}

}  // namespace seventhslide::test
