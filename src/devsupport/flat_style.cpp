#include "seventhslide/devsupport/flat_style.hpp"

#include "seventhslide/freeform/document.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/theme/theme_library.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::devsupport {
namespace {

using seventhslide::json::bool_or;
using seventhslide::json::int_or;
using seventhslide::json::num_or;
using seventhslide::json::str_or;

[[nodiscard]] std::string family_or(const nlohmann::json& style, const char* key,
                                    const std::string& fallback)
{
    auto v = str_or(style, key, {});
    return v.empty() ? fallback : v;
}

[[nodiscard]] int size_or(const nlohmann::json& style, const char* key, int fallback)
{
    const int v = int_or(style, key, 0);
    return v > 0 ? v : fallback;
}

}  // namespace

theme::ElementSet elements_from_flat_style(const nlohmann::json& style)
{
    const double cw = geom::k_reference_canvas.width;
    const double ch = geom::k_reference_canvas.height;
    theme::ElementSet set = theme::default_elements();

    const auto by_id = [](std::vector<theme::Element>& list,
                          std::string_view id) -> theme::Element* {
        for (auto& el : list) {
            if (el.object.id == id) {
                return &el;
            }
        }
        return nullptr;
    };
    const auto drop_unless = [&style](std::vector<theme::Element>& list, std::string_view id,
                                      const char* switch_key) {
        if (bool_or(style, switch_key, true)) {
            return;
        }
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [id](const theme::Element& el) { return el.object.id == id; }),
                   list.end());
    };

    const auto base_family = family_or(style, "font_family", std::string(canvas::k_default_font_family));
    const int base_size = size_or(style, "font_size", 48);

    if (auto* el = by_id(set.song, "te_lyrics")) {
        auto& o = el->object;
        o.x = num_or(style, "box_x", 320);
        o.y = num_or(style, "box_y", 340);
        o.w = num_or(style, "width_px", 1280);
        o.h = num_or(style, "height_px", 400);
        o.font_family = base_family;
        o.font_size = base_size;
        o.bold = bool_or(style, "font_bold", false);
        o.italic = bool_or(style, "font_italic", false);
        // The lyrics box has no colour key of its own: the song template paints
        // it in ``highlight_color``, which doubles as the follow-lines active tint.
        o.font_color = str_or(style, "highlight_color", "#ffffff");
        o.text_align = str_or(style, "align", "center");
        o.vertical_align = freeform::read_vertical_align(str_or(style, "valign", "middle"));
        o.line_height = layout::clamp_overlay_line_height(num_or(style, "line_height", 1.2));
        o.letter_spacing =
            layout::clamp_overlay_letter_spacing(num_or(style, "letter_spacing", 0));
        o.opacity = num_or(style, "text_opacity", 1.0);
        // The lines' types, as a design stores them.
        if (const auto* type = seventhslide::json::find(style, "active_line")) {
            el->body.active_line = type->get<canvas::LineType>();
        }
        if (const auto* type = seventhslide::json::find(style, "other_lines")) {
            el->body.other_lines = type->get<canvas::LineType>();
        }
        el->body.show_chords = bool_or(style, "show_chords", false);
        const layout::ChordStyle chords;
        el->body.chords = layout::clamped(layout::ChordStyle{
            .size_pct = int_or(style, "chord_size_pct", chords.size_pct),
            .gap_pct = int_or(style, "chord_gap_pct", chords.gap_pct),
            .color = str_or(style, "chord_color", chords.color),
        });
    }

    if (auto* el = by_id(set.bible, "te_bible_text")) {
        auto& o = el->object;
        o.x = num_or(style, "bible_text_box_x", 320);
        o.y = num_or(style, "bible_text_box_y", 340);
        o.w = num_or(style, "bible_text_box_width", 1280);
        o.h = num_or(style, "bible_text_box_height", 400);
        o.font_family = family_or(style, "bible_main_font_family", base_family);
        o.font_size = size_or(style, "bible_main_font_size", base_size);
        o.bold = bool_or(style, "bible_main_font_bold", false);
        o.italic = bool_or(style, "bible_main_font_italic", false);
        o.font_color = str_or(style, "bible_text_color", "#ffffff");
        o.text_align = str_or(style, "bible_text_align", "center");
        o.vertical_align =
            freeform::read_vertical_align(str_or(style, "bible_text_valign", "middle"));
        o.line_height =
            layout::clamp_overlay_line_height(num_or(style, "bible_text_line_height", 1.2));
        o.letter_spacing =
            layout::clamp_overlay_letter_spacing(num_or(style, "bible_text_letter_spacing", 0));
        o.opacity = num_or(style, "bible_text_opacity", 1.0);
        auto& b = el->body;
        b.verse_numbers = bool_or(style, "show_bible_verse_numbers", false);
        b.verse_font_family = family_or(style, "bible_verse_font_family", base_family);
        b.verse_font_size = size_or(style, "bible_verse_font_size", 24);
        b.verse_font_bold = bool_or(style, "bible_verse_font_bold", false);
        b.verse_font_italic = bool_or(style, "bible_verse_font_italic", false);
        b.verse_color = str_or(style, "bible_verse_color", "#ffffff");
        b.verse_valign = str_or(style, "bible_verse_valign", "super");
        b.verse_margin_right_pct = int_or(style, "bible_verse_margin_right_pct", 40);
    }

    const auto place_area = [&style, cw, ch](theme::Element& el, const char* xk, const char* yk,
                                             const char* wk, const char* hk) {
        el.object.x = num_or(style, xk, 0);
        el.object.y = num_or(style, yk, 0);
        const double w = num_or(style, wk, 0);
        const double h = num_or(style, hk, 0);
        // 0 means "fill the canvas" in the flat keys; an object has to say it.
        el.object.w = w > 0 ? w : cw;
        el.object.h = h > 0 ? h : ch;
    };
    // The flat keys had one video area, for videos and live sources alike.
    if (auto* el = by_id(set.video, "te_video_area")) {
        place_area(*el, "video_area_x", "video_area_y", "video_area_width", "video_area_height");
    }
    if (auto* el = by_id(set.live, "te_live_area")) {
        place_area(*el, "video_area_x", "video_area_y", "video_area_width", "video_area_height");
    }
    if (auto* el = by_id(set.image, "te_image_area")) {
        place_area(*el, "image_area_x", "image_area_y", "image_area_width", "image_area_height");
        el->object.object_fit = str_or(style, "image_fit", "contain");
    }

    drop_unless(set.song, "te_lyrics", "show_lyrics");
    drop_unless(set.bible, "te_bible_text", "show_bible_text");
    drop_unless(set.video, "te_video_area", "video_enabled");
    drop_unless(set.live, "te_live_area", "video_enabled");
    drop_unless(set.image, "te_image_area", "image_enabled");
    return set;
}

void install_flat_style(model::OutputConfig& oc, const nlohmann::json& flat)
{
    for (const auto kind : theme::k_theme_kinds) {
        if (!theme::theme_list(oc, kind).empty()) {
            return;
        }
    }
    const nlohmann::json style = flat.is_object() ? flat : nlohmann::json::object();
    // One look for every kind of content: each kind's theme is the same flat
    // description, and of its design the contexts of its kind. Themes are
    // looked up by kind, so they share an id.
    const auto design = elements_from_flat_style(style).to_json();
    for (const auto kind : theme::k_theme_kinds) {
        model::ThemeEntry entry;
        entry.id = kind == theme::ThemeKind::Bg ? "flat-bg" : "flat";
        entry.name = "Flat";
        entry.style = style;
        if (kind != theme::ThemeKind::Bg) {
            entry.style[std::string(theme::k_elements_key)] =
                theme::own_contexts_only(kind, design);
        }
        theme::theme_list(oc, kind).push_back(std::move(entry));
    }
    if (!oc.category_defaults.is_object() || oc.category_defaults.empty()) {
        oc.category_defaults = theme::first_category_defaults(oc);
    }
}

}  // namespace seventhslide::devsupport
