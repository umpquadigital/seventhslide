#pragma once

// The look one output paints for one theme selection.
//
// A text theme's design is its element model; a background theme is a fill.
// Resolve reads those once, fits the elements to the output's canvas, and
// lands the body boxes, media rectangles and behaviour flags here. Layout and
// the scene builder read this struct; they do not search the element list.
//
// Seam: plain C++20.

#include "seventhslide/canvas/box.hpp"
#include "seventhslide/canvas/property_value.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/layout/background_fill.hpp"
#include "seventhslide/model/output_config.hpp"
#include "seventhslide/theme/theme_elements.hpp"
#include "seventhslide/theme/theme_resolver.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <iterator>
#include <string_view>

namespace seventhslide::theme {

/// Where a body box sits when the theme has none: still laid out, never shown.
/// (A function, not a constant: a constexpr object holding a std::string only
/// compiles where short strings stay inline during constant evaluation.)
[[nodiscard]] inline canvas::TextBox default_body_box()
{
    canvas::TextBox box;
    box.x = 320;
    box.y = 340;
    box.width = 1280;
    box.height = 400;
    return box;
}

/// The song body box, from the Lyrics element. ``shown`` is false when the
/// theme has no lyrics box. Follow-lines sets the active line and the other
/// lines each in its type over the box's (``canvas::painted_type``).
struct SongBox : canvas::TextBox {
    SongBox() : canvas::TextBox(default_body_box()) {}

    bool shown = false;
    /// The Lyrics element's place in the stack among the theme's elements.
    int z = 0;
    canvas::LineType active_line;
    canvas::LineType other_lines{.color = std::string(k_default_dim_color)};
    bool show_chords = false;
    layout::ChordStyle chords;

    [[nodiscard]] bool operator==(const SongBox&) const noexcept = default;
};

/// The passage box, from the BibleText element. An empty family or a size of
/// 0 has already fallen back to the song box.
struct BibleBox : canvas::TextBox {
    BibleBox() : canvas::TextBox(default_body_box()) {}

    bool shown = false;
    /// The BibleText element's place in the stack among the theme's elements.
    int z = 0;
    bool show_verse_numbers = false;
    std::string verse_font_family{canvas::k_default_font_family};
    int verse_font_size = 24;
    bool verse_font_bold = false;
    bool verse_font_italic = false;
    std::string verse_color = "#ffffff";
    std::string verse_valign = "super";
    int verse_margin_right_pct = 40;

    [[nodiscard]] bool operator==(const BibleBox&) const noexcept = default;
};

/// A video, still or web-page rectangle. ``enabled`` is false when the theme
/// has no such element, and that media shows nowhere. ``fit`` is the image or
/// web area's object-fit.
struct MediaRect {
    bool enabled = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    /// The area element's place in the stack among the theme's elements.
    int z = 0;
    std::string fit = "contain";

    [[nodiscard]] bool operator==(const MediaRect&) const noexcept = default;
};

/// The key a text theme stores its transitions under: one per content
/// context, by the context's name (``{"song": {type, duration_ms}, ...}``).
inline constexpr std::string_view k_transitions_key = "transitions";

/// Layout policy stored on the text theme, not on any one element, and the
/// transition of each content type.
struct ThemeBehavior {
    bool balance_wrapped_lines = false;
    int balance_wrapped_strength = 100;
    bool bible_fluid_verses = false;
    /// How each content context's slides change, which is also how one of
    /// its items comes in, in ``k_content_contexts`` order.
    std::array<model::Transition, std::size(k_content_contexts)> transitions{};
    bool fluid_slides = false;
    int follow_lines = 0;
    bool prevent_mixed_active = false;
    bool show_title_slides = true;
    bool title_slide_show_lines = true;
    int verse_gap = 0;

    /// ``ctx``'s transition (``settings_context``: Titles is the Song's).
    [[nodiscard]] const model::Transition& transition(ElementContext ctx) const noexcept;
    [[nodiscard]] model::Transition& transition(ElementContext ctx) noexcept;

    [[nodiscard]] bool operator==(const ThemeBehavior&) const noexcept = default;
};

/// The background theme.
struct ThemeBackground {
    layout::BackgroundType type = layout::BackgroundType::Transparent;
    std::string color{layout::k_default_fill_color};
    std::string image;
    std::string video;
    bool enable_fade = false;
    int fade_duration = model::k_default_transition_ms;

    /// What it paints, as a slide's own background holds it.
    [[nodiscard]] layout::BackgroundFill fill() const;

    [[nodiscard]] bool operator==(const ThemeBackground&) const noexcept = default;
};

struct ResolvedTheme {
    SongBox song;
    BibleBox bible;
    MediaRect video;
    MediaRect image;
    MediaRect web;
    /// Where a live source is placed: the Live context's video area.
    MediaRect live;
    ThemeBehavior behavior;
    ThemeBackground background;
    /// Fitted to the output canvas. Scene objects that are not body boxes
    /// (copyright, clocks, indicators) are painted from here.
    ElementSet elements;

    [[nodiscard]] bool operator==(const ResolvedTheme&) const noexcept = default;
};

/**
 * The theme ``oc`` paints for ``sel``.
 *
 * Text-theme behaviour and the element model come from the resolved text
 * theme; the fill comes from the resolved background theme. Missing themes
 * leave the struct defaults, and a text theme with no element model, or one
 * the content needs whose id names nothing, paints the stock design. Content
 * that takes no text theme (``theme_needs``) has no elements at all. Elements
 * are fitted to ``oc``'s canvas here, once.
 */
[[nodiscard]] ResolvedTheme resolve_theme(const model::OutputConfig& oc,
                                          const ThemeSelection& sel);

/// The spans read_theme_behavior holds its numbers to.
inline constexpr canvas::Range k_follow_lines_range{0, 20};
/// In canvas pixels, which a theme fitted to a larger canvas scales.
inline constexpr canvas::Range k_verse_gap_range{0, geom::k_max_canvas.height};
inline constexpr canvas::Range k_balance_strength_range{0, 100};

/// The theme-wide settings a text theme's ``style`` names, each one it leaves
/// out at its default: what playback reads and what the theme designer shows.
/// A number out of its span reads as the nearest end.
[[nodiscard]] ThemeBehavior read_theme_behavior(const nlohmann::json& style);

/// The spans above by their keys in ``style``: ``{"follow_lines": {"min": 0,
/// "max": 20}, …}``, for the designer's fields.
[[nodiscard]] nlohmann::json theme_behavior_ranges();

/// Set ``ctx``'s transition in ``style``. The first one set writes every
/// context's out (each at what it read as).
void set_context_transition(nlohmann::json& style,
                            ElementContext ctx,
                            const model::Transition& spec);

/// The same for a background theme's fill and fade.
[[nodiscard]] ThemeBackground read_theme_background(const nlohmann::json& style);

/**
 * The same resolve from style blobs the theme library already stores.
 *
 * ``text_style`` is a text theme (behaviour keys + ``elements``).
 * ``bg_style`` is a background theme. Either may be empty.
 */
[[nodiscard]] ResolvedTheme resolve_styles(const nlohmann::json& text_style,
                                           const nlohmann::json& bg_style,
                                           geom::CanvasSize canvas);


}  // namespace seventhslide::theme
