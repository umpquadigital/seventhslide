#include "seventhslide/theme/resolved_theme.hpp"

#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/json/read.hpp"
#include "seventhslide/layout/background_fill.hpp"
#include "seventhslide/model/output_library.hpp"
#include "seventhslide/theme/theme_library.hpp"

#include <cmath>
#include <utility>

namespace seventhslide::theme {
namespace {

[[nodiscard]] int px(double v) noexcept
{
    return static_cast<int>(std::lround(v));
}

void fit_element_set(ElementSet& set, int& verse_gap, geom::CanvasSize canvas)
{
    if (!canvas.valid() || set.canvas == canvas) {
        return;
    }
    const auto mapping = geom::CanvasMapping::stretch(set.canvas, canvas);
    for (const auto ctx : k_element_contexts) {
        for (auto& el : set.at(ctx)) {
            map_element(el, mapping);
        }
    }
    verse_gap = mapping.pixels(verse_gap);
    set.canvas = canvas;
}

/// ``o`` as a body box: its words' rectangle in whole pixels (the element's
/// less its margins), its type, its opacity, its frame and its effects.
[[nodiscard]] canvas::TextBox text_box_of(const freeform::Object& o)
{
    canvas::TextBox box;
    // The words' rectangle: the element's, less its margins; the frame is
    // painted back out to the element's.
    const auto& m = o.paragraph.margins;
    box.x = px(o.x + std::max(0.0, m.left));
    box.y = px(o.y + std::max(0.0, m.top));
    box.width = std::max(1, px(o.w - std::max(0.0, m.left) - std::max(0.0, m.right)));
    box.height = std::max(1, px(o.h - std::max(0.0, m.top) - std::max(0.0, m.bottom)));
    box.margins = m;
    box.font_family = o.font_family;
    box.font_size = o.font_size;
    box.bold = o.bold;
    box.italic = o.italic;
    box.underline = o.underline;
    box.color = o.font_color;
    box.align = o.text_align;
    box.valign = o.vertical_align;
    box.line_height = o.line_height;
    box.letter_spacing = o.letter_spacing;
    box.opacity = o.opacity;
    box.fill = o.fill;
    box.line = o.line;
    box.effects = o.effects;
    return box;
}

/// ``o`` as a media area: shown, its rectangle in whole pixels and its place
/// in the stack. The fit stays the default; an image area takes the element's
/// own, and video fit is the painter's.
[[nodiscard]] MediaRect media_area_of(const freeform::Object& o)
{
    MediaRect area;
    area.enabled = true;
    area.x = px(o.x);
    area.y = px(o.y);
    area.width = px(o.w);
    area.height = px(o.h);
    area.z = o.z;
    return area;
}

void project_song(SongBox& box, const Element& el)
{
    static_cast<canvas::TextBox&>(box) = text_box_of(el.object);
    box.shown = true;
    box.z = el.object.z;
    box.active_line = el.body.active_line;
    box.other_lines = el.body.other_lines;
    box.show_chords = el.body.show_chords;
    box.chords = el.body.chords;
}

void project_bible(BibleBox& box, const Element& el)
{
    static_cast<canvas::TextBox&>(box) = text_box_of(el.object);
    box.shown = true;
    box.z = el.object.z;
    box.show_verse_numbers = el.body.verse_numbers;
    box.verse_font_family = el.body.verse_font_family;
    box.verse_font_size = el.body.verse_font_size;
    box.verse_font_bold = el.body.verse_font_bold;
    box.verse_font_italic = el.body.verse_font_italic;
    box.verse_color = el.body.verse_color;
    box.verse_valign = el.body.verse_valign;
    box.verse_margin_right_pct = el.body.verse_margin_right_pct;
}

void project_boxes(ResolvedTheme& theme)
{
    theme.song.shown = false;
    theme.bible.shown = false;
    theme.video.enabled = false;
    theme.image.enabled = false;
    theme.web.enabled = false;

    for (const auto& el : theme.elements.song) {
        if (el.binding == ElementBinding::Lyrics) {
            project_song(theme.song, el);
        }
    }
    for (const auto& el : theme.elements.bible) {
        if (el.binding == ElementBinding::BibleText) {
            project_bible(theme.bible, el);
        }
    }
    theme.live.enabled = false;
    // Each media type's area is in its own context.
    for (const auto& el : theme.elements.image) {
        if (el.binding == ElementBinding::ImageArea) {
            theme.image = media_area_of(el.object);
            theme.image.fit = el.object.object_fit;
        }
    }
    for (const auto& el : theme.elements.video) {
        if (el.binding == ElementBinding::VideoArea) {
            theme.video = media_area_of(el.object);
        }
    }
    for (const auto& el : theme.elements.web) {
        if (el.binding == ElementBinding::WebArea) {
            theme.web = media_area_of(el.object);
            theme.web.fit = el.object.object_fit;
        }
    }
    for (const auto& el : theme.elements.live) {
        if (el.binding == ElementBinding::VideoArea) {
            theme.live = media_area_of(el.object);
        }
    }

    if (theme.bible.font_family.empty()) {
        theme.bible.font_family = theme.song.font_family;
    }
    if (theme.bible.font_size <= 0) {
        theme.bible.font_size = theme.song.font_size;
    }
}

}  // namespace

namespace {

[[nodiscard]] std::size_t content_index(ElementContext ctx) noexcept
{
    const auto settings = settings_context(ctx);
    for (std::size_t i = 0; i < std::size(k_content_contexts); ++i) {
        if (k_content_contexts[i] == settings) {
            return i;
        }
    }
    return 0;
}

}  // namespace

const model::Transition& ThemeBehavior::transition(ElementContext ctx) const noexcept
{
    return transitions[content_index(ctx)];
}

model::Transition& ThemeBehavior::transition(ElementContext ctx) noexcept
{
    return transitions[content_index(ctx)];
}

ThemeBehavior read_theme_behavior(const nlohmann::json& style)
{
    ThemeBehavior behavior;
    if (const auto* stored = json::find(style, k_transitions_key.data());
        stored != nullptr && stored->is_object()) {
        for (const auto ctx : k_content_contexts) {
            if (const auto* one = json::find(*stored, std::string(to_string(ctx)).c_str());
                one != nullptr && one->is_object()) {
                behavior.transition(ctx) = one->get<model::Transition>();
            }
        }
    }
    behavior.balance_wrapped_lines =
        json::bool_or(style, "balance_wrapped_lines", behavior.balance_wrapped_lines);
    behavior.balance_wrapped_strength = k_balance_strength_range.clamp(
        json::style_int(style, "balance_wrapped_strength", behavior.balance_wrapped_strength));
    behavior.bible_fluid_verses =
        json::bool_or(style, "bible_fluid_verses", behavior.bible_fluid_verses);
    behavior.fluid_slides = json::bool_or(style, "fluid_slides", behavior.fluid_slides);
    behavior.follow_lines =
        k_follow_lines_range.clamp(json::style_int(style, "follow_lines", behavior.follow_lines));
    behavior.prevent_mixed_active =
        json::bool_or(style, "prevent_mixed_active", behavior.prevent_mixed_active);
    behavior.show_title_slides =
        json::bool_or(style, "show_title_slides", behavior.show_title_slides);
    behavior.title_slide_show_lines =
        json::bool_or(style, "title_slide_show_lines", behavior.title_slide_show_lines);
    behavior.verse_gap =
        k_verse_gap_range.clamp(json::style_int(style, "verse_gap", behavior.verse_gap));
    return behavior;
}

nlohmann::json theme_behavior_ranges()
{
    return {{"balance_wrapped_strength", k_balance_strength_range},
            {"follow_lines", k_follow_lines_range},
            {"verse_gap", k_verse_gap_range}};
}

void set_context_transition(nlohmann::json& style,
                            ElementContext ctx,
                            const model::Transition& spec)
{
    if (!style.is_object()) {
        style = nlohmann::json::object();
    }
    auto behavior = read_theme_behavior(style);
    behavior.transition(ctx) = spec;
    nlohmann::json all = nlohmann::json::object();
    for (const auto c : k_content_contexts) {
        all[std::string(to_string(c))] = behavior.transition(c);
    }
    style[std::string(k_transitions_key)] = std::move(all);
}

layout::BackgroundFill ThemeBackground::fill() const
{
    return {.type = type, .color = color, .image = image, .video = video};
}

ThemeBackground read_theme_background(const nlohmann::json& style)
{
    ThemeBackground background;
    // One reading of the fill keys and one of the fade's, the ones a Freeform
    // slide copying a theme uses. A style that is not an object keeps the
    // ThemeBackground defaults, which are the same strings the fill reader uses.
    if (const auto fill = layout::background_fill_from_style(style)) {
        background.type = fill->type;
        background.color = fill->color;
        background.image = fill->image;
        background.video = fill->video;
    }
    const auto fade = layout::background_fade_from_style(style);
    background.enable_fade = fade.enabled;
    background.fade_duration = fade.duration_ms;
    return background;
}

ResolvedTheme resolve_styles(const nlohmann::json& text_style,
                             const nlohmann::json& bg_style,
                             geom::CanvasSize canvas)
{
    ResolvedTheme theme;
    theme.behavior = read_theme_behavior(text_style);
    theme.background = read_theme_background(bg_style);
    // A theme with no element model (an empty one) paints the stock design.
    theme.elements = has_elements(text_style) ? ElementSet::from_json(element_model(text_style))
                                              : default_elements(canvas);
    fit_element_set(theme.elements, theme.behavior.verse_gap, canvas);
    project_boxes(theme);
    return theme;
}

ResolvedTheme resolve_theme(const model::OutputConfig& oc, const ThemeSelection& sel)
{
    const auto ids = resolve_theme_ids(oc, sel);
    const auto* bg = find_theme(oc.bg_themes, ids.bg_id);
    const nlohmann::json empty = nlohmann::json::object();
    const auto& bg_style = bg != nullptr && bg->style.is_object() ? bg->style : empty;
    const auto canvas = model::output_canvas(oc);
    const auto needs = theme_needs(sel.kind);
    if (!needs.theme) {
        // Content that takes no text theme (a divider, a Freeform slide)
        // paints nothing of one: not the stock design either.
        ResolvedTheme theme;
        theme.background = read_theme_background(bg_style);
        theme.elements.canvas = canvas;
        project_boxes(theme);
        return theme;
    }
    const auto* text = find_theme(theme_list(oc, text_kind_of(*needs.theme)), ids.theme_id);
    const auto& text_style = text != nullptr && text->style.is_object() ? text->style : empty;
    return resolve_styles(text_style, bg_style, canvas);
}

}  // namespace seventhslide::theme
