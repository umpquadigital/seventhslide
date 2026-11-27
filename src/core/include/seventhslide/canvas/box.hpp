#pragma once

// The one box geometry, the one box type style and the one resolved text box.
// Freeform objects, theme elements, theme body boxes and scene paint objects
// embed these instead of each keeping a copy of the same fields.
// Seam: plain C++20.

#include "seventhslide/canvas/parts.hpp"

#include <string>
#include <string_view>

namespace seventhslide::canvas {

/// Position and size. Overlay boxes use this; placed objects add rotation.
struct BoxRect {
    double x = 0;
    double y = 0;
    double w = 100;
    double h = 100;

    [[nodiscard]] bool operator==(const BoxRect&) const noexcept = default;
};

/// A placed canvas object: a rect plus rotation, opacity and flips.
struct BoxFrame : BoxRect {
    double rotation = 0;
    double opacity = 1;
    bool flip_h = false;
    bool flip_v = false;

    [[nodiscard]] bool operator==(const BoxFrame&) const noexcept = default;
};

/// The type a box has when it names none.
inline constexpr std::string_view k_default_font_family = "Liberation Sans";
inline constexpr int k_default_font_size = 48;
inline constexpr std::string_view k_default_font_color = "#ffffff";
inline constexpr std::string_view k_default_text_align = "center";
inline constexpr std::string_view k_default_vertical_align = "middle";
inline constexpr double k_default_line_height = 1.15;

/// Typography of a whole box. A run's own style overrides these at paint time.
struct BoxType {
    std::string font_family{k_default_font_family};
    int font_size = k_default_font_size;
    std::string font_color{k_default_font_color};
    bool bold = false;
    bool italic = false;
    bool underline = false;
    std::string text_align{k_default_text_align};
    std::string vertical_align{k_default_vertical_align};
    double line_height = k_default_line_height;
    int line_gap = 0;
    double letter_spacing = 0.0;

    [[nodiscard]] bool operator==(const BoxType&) const noexcept = default;
};

/// What a kind of box falls back to for the type fields it leaves unset.
struct TypeDefaults {
    int font_size = k_default_font_size;
    std::string_view text_align = k_default_text_align;
    std::string_view vertical_align = k_default_vertical_align;
};

/// Fill each type field ``type`` leaves empty (or at zero size) from
/// ``defaults`` and the default family and colour.
inline void fill_type_defaults(BoxType& type, const TypeDefaults& defaults = {})
{
    if (type.font_family.empty()) {
        type.font_family = k_default_font_family;
    }
    if (type.font_size <= 0) {
        type.font_size = defaults.font_size;
    }
    if (type.font_color.empty()) {
        type.font_color = k_default_font_color;
    }
    if (type.text_align.empty()) {
        type.text_align = defaults.text_align;
    }
    if (type.vertical_align.empty()) {
        type.vertical_align = defaults.vertical_align;
    }
}

/// A body text box resolved for one output: whole-pixel geometry, the type of
/// its text, its opacity, the frame behind its words (a fill and a line, as
/// any text box has) and its effects. The theme's song and Bible boxes are
/// these plus their own options, and the scene paints exactly this.
struct TextBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::string font_family{k_default_font_family};
    int font_size = k_default_font_size;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    std::string color{k_default_font_color};
    std::string align = "center";
    std::string valign = "middle";
    double line_height = 1.2;
    double letter_spacing = 0.0;
    double opacity = 1.0;
    Fill fill;
    Line line;
    Effects effects;
    /// The box's rectangle is where its words go; the frame reaches past it
    /// by these (the element's margins, which the words keep inside).
    Margins margins;

    [[nodiscard]] bool operator==(const TextBox&) const noexcept = default;
};

/// The type a line of a ``TextBox`` is painted in, nothing left to the box.
struct PaintedType {
    std::string family;
    int size = k_default_font_size;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    std::string color;

    [[nodiscard]] bool operator==(const PaintedType&) const noexcept = default;
};

/// ``type`` over ``box``: the box's type with each field the line sets in
/// place of the box's.
[[nodiscard]] inline PaintedType painted_type(const TextBox& box, const LineType& type)
{
    return PaintedType{
        .family = type.family.empty() ? box.font_family : type.family,
        .size = type.size > 0 ? type.size : box.font_size,
        .bold = type.bold.value_or(box.bold),
        .italic = type.italic.value_or(box.italic),
        .underline = type.underline.value_or(box.underline),
        .color = type.color.empty() ? box.color : type.color,
    };
}

}  // namespace seventhslide::canvas
