#include "seventhslide/freeform/object_properties.hpp"

#include "seventhslide/freeform/doc_edit.hpp"
#include "seventhslide/freeform/paint.hpp"
#include "seventhslide/geom/shape_path.hpp"
#include "seventhslide/layout/overlay_layout.hpp"
#include "seventhslide/layout/overlays.hpp"
#include "seventhslide/layout/text_measurer.hpp"
#include "seventhslide/model/rich_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace seventhslide::freeform {
namespace {

namespace pv = property_value;
using json = nlohmann::json;

using Apply = EditResult (*)(Object&, const json&);
using canvas::Range;

/// One property: its name, its writer and, for a number held to a span, the
/// span its writer clamps to.
struct Property {
    std::string_view name;
    Apply apply;
    std::optional<Range> range = std::nullopt;
};

// The spans the writers below clamp to, by what they hold.
constexpr Range k_size_range{0.0, k_max_stroke_width};
constexpr Range k_crop_range{0.0, canvas::k_max_crop};
constexpr Range k_share_range{0.0, 1.0};
constexpr Range k_font_size_range{8, k_max_font_size};
constexpr Range k_line_height_range{layout::k_min_line_height, layout::k_max_line_height};
constexpr Range k_line_gap_range{0, layout::k_max_line_gap};
constexpr Range k_letter_spacing_range{layout::k_min_letter_spacing_px, layout::k_max_letter_spacing_px};
constexpr Range k_columns_range{1, layout::k_max_columns};
constexpr Range k_table_side_range{1, canvas::k_max_table_side};
constexpr Range k_table_border_range{0.0, 40.0};
constexpr Range k_corner_range{geom::k_corner_min, geom::k_corner_max};
constexpr Range k_points_range{geom::k_points_min, geom::k_points_max};
constexpr Range k_inner_range{geom::k_inner_min, geom::k_inner_max};
/// The widest head any kind takes; a double arrow's is narrower
/// (geom::head_max), which object_property_ranges gives for one.
constexpr Range k_head_range{geom::k_head_min, 1.0};
constexpr Range k_thickness_range{geom::k_thickness_min, geom::k_thickness_max};
constexpr Range k_indicator_gap_range{canvas::k_min_indicator_gap, canvas::k_max_indicator_gap};
constexpr Range k_chord_size_range{layout::k_min_chord_size_pct, layout::k_max_chord_size_pct};
constexpr Range k_chord_gap_range{0, layout::k_max_chord_gap_pct};

/// Bold, italic or underline on a whole box: the box's own, and in a linked box
/// the same on every word it has styled.
[[nodiscard]] EditResult set_box_flag(Object& o, bool& field, bool model::TextStyle::*style,
                                      const json& v)
{
    if (auto result = set_flag_field(field, v); !result) {
        return result;
    }
    for (auto& [id, overlay] : o.cue_overlays) {
        auto lines = model::parse_markup_lines(overlay);
        for (auto& line : lines) {
            for (auto& run : line) {
                run.style.*style = field;
            }
        }
        overlay = model::format_markup_lines(lines);
    }
    return EditResult::success();
}

/// A follow cue's flag from a bool; the box's own (``box``), or null, has it
/// follow the box.
[[nodiscard]] EditResult set_upcoming_flag(std::optional<bool>& field, bool box, const json& v)
{
    bool on = box;
    if (!v.is_null()) {
        if (auto result = set_flag_field(on, v); !result) {
            return result;
        }
    }
    field = on == box ? std::nullopt : std::optional<bool>(on);
    return EditResult::success();
}

/// A string from a fixed set of values.
[[nodiscard]] bool one_of(const std::string& s, std::initializer_list<std::string_view> values)
{
    return std::find(values.begin(), values.end(), s) != values.end();
}

/// A colour field where empty means none (a shape's fill or outline).
[[nodiscard]] EditResult set_optional_color(std::string& field, const json& v)
{
    auto s = pv::text(v);
    if (!s) {
        return EditResult::fail("expected a color");
    }
    field = std::move(*s);
    return EditResult::success();
}

/// A size in canvas pixels (a blur, a distance, a width): 0 or more, and no
/// wider than an outline can be.
[[nodiscard]] EditResult set_size_field(double& field, const json& v)
{
    const auto n = pv::number(v);
    if (!n) {
        return EditResult::fail("expected a size");
    }
    field = k_size_range.clamp(*n);
    return EditResult::success();
}

/// A crop fraction: 0 to k_max_crop of the picture off one edge.
[[nodiscard]] EditResult set_crop_field(double& field, const json& v)
{
    const auto n = pv::number(v);
    if (!n) {
        return EditResult::fail("expected a crop");
    }
    field = k_crop_range.clamp(*n);
    return EditResult::success();
}

/// A setting made on the whole box: ``clear`` takes it off each paragraph
/// that had its own, so the box's holds for all of them.
template <class Clear>
void clear_line_formats(Object& o, Clear clear)
{
    if (o.type != ObjectType::Text || o.text.find("<p=") == std::string::npos) {
        return;
    }
    auto lines = model::parse_markup_lines(o.text);
    for (auto& line : lines) {
        for (auto& span : line) {
            clear(span.style.line);
        }
    }
    o.text = model::format_markup_lines(lines);
}

/// A share of a box: 0 to 1.
[[nodiscard]] EditResult set_share_field(double& field, const json& v)
{
    const auto n = pv::number(v);
    if (!n) {
        return EditResult::fail("expected a share");
    }
    field = k_share_range.clamp(*n);
    return EditResult::success();
}

/// An angle in degrees, brought into one turn.
[[nodiscard]] EditResult set_angle_field(double& field, const json& v)
{
    const auto n = pv::number(v);
    if (!n) {
        return EditResult::fail("expected an angle");
    }
    field = normalized_rotation(*n);
    return EditResult::success();
}

/// An open outline's end: none, an arrow or a dot.
[[nodiscard]] EditResult set_line_end(canvas::LineEnd& field, const json& v)
{
    const auto end = pv::named(
        v, [](std::string_view s) { return canvas::line_end_from_string(s); },
        [](canvas::LineEnd e) { return canvas::to_string(e); });
    if (!end) {
        return EditResult::fail("invalid line end");
    }
    field = *end;
    return EditResult::success();
}

/// A table's rows (``rows``) or columns, its cells kept where they were and
/// any new ones empty.
[[nodiscard]] EditResult set_table_size(canvas::Table& table, bool rows, const json& v)
{
    const auto n = pv::whole(v);
    if (!n) {
        return EditResult::fail("expected a number of cells");
    }
    if (*n == (rows ? table.rows : table.columns)) {
        return EditResult::success();
    }
    const int want = k_table_side_range.clamp(*n);
    const int new_rows = rows ? want : table.rows;
    const int new_cols = rows ? table.columns : want;
    std::vector<std::string> cells(static_cast<std::size_t>(new_rows) * static_cast<std::size_t>(new_cols));
    for (int r = 0; r < std::min(new_rows, table.rows); ++r) {
        for (int c = 0; c < std::min(new_cols, table.columns); ++c) {
            const auto from = static_cast<std::size_t>(r * table.columns + c);
            if (from < table.cells.size()) {
                cells[static_cast<std::size_t>(r * new_cols + c)] = table.cells[from];
            }
        }
    }
    table.rows = new_rows;
    table.columns = new_cols;
    table.cells = std::move(cells);
    return EditResult::success();
}

/// A gradient's first colour (``from``) or its last: its first stop, or its
/// last, made when it has none.
[[nodiscard]] EditResult set_gradient_end(canvas::Gradient& gradient, bool from, const json& v)
{
    // What a box with no gradient reads: nothing to change.
    if (v.is_string() && v.get_ref<const std::string&>().empty()) {
        return EditResult::success();
    }
    std::string color;
    if (auto result = set_color_field(color, v); !result) {
        return result;
    }
    if (gradient.stops.size() < 2) {
        gradient.stops = {{0.0, "#ffffff"}, {1.0, "#000000"}};
    }
    (from ? gradient.stops.front() : gradient.stops.back()).color = std::move(color);
    return EditResult::success();
}

/// One of the shape's adjustment knobs, kept in the kind's legal window.
template <class Write>
[[nodiscard]] EditResult set_adjust(Object& o, const json& v, Write write)
{
    const auto n = pv::number(v);
    if (!n) {
        return EditResult::fail("expected a number");
    }
    auto adjust = o.shape_adjust;
    write(adjust, *n);
    o.shape_adjust = geom::clamp_adjust(o.shape_kind, adjust);
    return EditResult::success();
}

constexpr std::array k_properties = {
    Property{"font_family",
             [](Object& o, const json& v) {
                 auto s = pv::text(v);
                 if (!s || s->empty()) {
                     return EditResult::fail("font required");
                 }
                 o.font_family = std::move(*s);
                 return EditResult::success();
             }},
    Property{"font_size",
             [](Object& o, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a font size");
                 }
                 o.font_size = k_font_size_range.clamp(*n);
                 return EditResult::success();
             },
             k_font_size_range},
    Property{"font_color", [](Object& o, const json& v) { return set_color_field(o.font_color, v); }},
    Property{"bold",
             [](Object& o, const json& v) {
                 return set_box_flag(o, o.bold, &model::TextStyle::bold, v);
             }},
    Property{"italic",
             [](Object& o, const json& v) {
                 return set_box_flag(o, o.italic, &model::TextStyle::italic, v);
             }},
    Property{"underline",
             [](Object& o, const json& v) {
                 return set_box_flag(o, o.underline, &model::TextStyle::underline, v);
             }},
    Property{"align",
             [](Object& o, const json& v) {
                 auto s = pv::text(v);
                 if (!s || !one_of(*s, {"left", "center", "right"})) {
                     return EditResult::fail("invalid align");
                 }
                 o.text_align = std::move(*s);
                 clear_line_formats(o, [](model::LineFormat& f) { f.align.clear(); });
                 return EditResult::success();
             }},
    Property{"valign",
             [](Object& o, const json& v) {
                 auto s = pv::text(v);
                 if (!s || !one_of(*s, {"top", "middle", "bottom"})) {
                     return EditResult::fail("invalid valign");
                 }
                 o.vertical_align = std::move(*s);
                 return EditResult::success();
             }},
    Property{"line_height",
             [](Object& o, const json& v) {
                 const auto n = pv::number(v);
                 if (!n) {
                     return EditResult::fail("expected a line height");
                 }
                 o.line_height = layout::clamp_overlay_line_height(*n);
                 clear_line_formats(o, [](model::LineFormat& f) { f.line_pct = 0; });
                 return EditResult::success();
             },
             k_line_height_range},
    Property{"line_gap",
             [](Object& o, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a line gap");
                 }
                 o.line_gap = layout::clamp_overlay_line_gap(*n);
                 return EditResult::success();
             },
             k_line_gap_range},
    Property{"letter_spacing",
             [](Object& o, const json& v) {
                 const auto n = pv::number(v);
                 if (!n) {
                     return EditResult::fail("expected a letter spacing");
                 }
                 o.letter_spacing = layout::clamp_overlay_letter_spacing(*n);
                 return EditResult::success();
             },
             k_letter_spacing_range},
    Property{"opacity",
             [](Object& o, const json& v) {
                 const auto n = pv::number(v);
                 if (!n) {
                     return EditResult::fail("expected an opacity");
                 }
                 o.opacity = k_share_range.clamp(*n);
                 return EditResult::success();
             },
             k_share_range},
    Property{"object_fit",
             [](Object& o, const json& v) {
                 auto s = pv::text(v);
                 if (!s || !one_of(*s, {"contain", "cover", "fill"})) {
                     return EditResult::fail("invalid object-fit");
                 }
                 o.object_fit = std::move(*s);
                 return EditResult::success();
             }},
    Property{"filename",
             [](Object& o, const json& v) {
                 auto s = pv::text(v);
                 if (!s || s->empty()) {
                     return EditResult::fail("image filename required");
                 }
                 o.filename = std::move(*s);
                 return EditResult::success();
             }},
    Property{"fill", [](Object& o, const json& v) { return set_optional_color(o.fill.color, v); }},
    Property{"fill_kind",
             [](Object& o, const json& v) {
                 const auto kind = pv::named(
                     v, [](std::string_view s) { return canvas::fill_kind_from_string(s); },
                     [](canvas::FillKind k) { return canvas::to_string(k); });
                 if (!kind) {
                     return EditResult::fail("invalid fill kind");
                 }
                 o.fill.kind = *kind;
                 // Solid with no colour would paint nothing: a shape takes a
                 // new shape's colour, a box a translucent one its text
                 // shows through, for the operator to change.
                 if (*kind == canvas::FillKind::Solid && o.fill.color.empty()) {
                     o.fill.color = std::string(o.type == ObjectType::Shape ? k_default_shape_fill
                                                                            : k_default_box_fill);
                 }
                 // A gradient starts from the box's colour to black.
                 if (*kind == canvas::FillKind::Gradient && o.fill.gradient.stops.size() < 2) {
                     const std::string from =
                         o.fill.color.empty() ? std::string("#ffffff") : o.fill.color;
                     o.fill.gradient.stops = {{0.0, from}, {1.0, "#000000"}};
                 }
                 return EditResult::success();
             }},
    // A gradient's: its kind, its direction, and the colours at its ends
    // (the first stop and the last; any between are kept).
    Property{"gradient_kind",
             [](Object& o, const json& v) {
                 const auto kind = pv::named(
                     v, [](std::string_view s) { return canvas::gradient_kind_from_string(s); },
                     [](canvas::GradientKind k) { return canvas::to_string(k); });
                 if (!kind) {
                     return EditResult::fail("invalid gradient");
                 }
                 o.fill.gradient.kind = *kind;
                 return EditResult::success();
             }},
    Property{"gradient_angle",
             [](Object& o, const json& v) { return set_angle_field(o.fill.gradient.angle, v); }},
    Property{"gradient_center_x",
             [](Object& o, const json& v) { return set_share_field(o.fill.gradient.center_x, v); },
             k_share_range},
    Property{"gradient_center_y",
             [](Object& o, const json& v) { return set_share_field(o.fill.gradient.center_y, v); },
             k_share_range},
    Property{"gradient_from",
             [](Object& o, const json& v) { return set_gradient_end(o.fill.gradient, true, v); }},
    Property{"gradient_to",
             [](Object& o, const json& v) { return set_gradient_end(o.fill.gradient, false, v); }},
    Property{"fill_picture_file",
             [](Object& o, const json& v) {
                 auto s = pv::text(v);
                 if (!s || s->empty()) {
                     return EditResult::fail("picture filename required");
                 }
                 o.fill.picture.file = std::move(*s);
                 o.fill.kind = canvas::FillKind::Picture;
                 return EditResult::success();
             }},
    Property{"fill_picture_fit",
             [](Object& o, const json& v) {
                 auto s = pv::text(v);
                 if (!s || !one_of(*s, {"contain", "cover", "fill"})) {
                     return EditResult::fail("invalid object-fit");
                 }
                 o.fill.picture.fit = std::move(*s);
                 return EditResult::success();
             }},
    Property{"stroke", [](Object& o, const json& v) { return set_optional_color(o.line.color, v); }},
    Property{"line_dash",
             [](Object& o, const json& v) {
                 const auto dash = pv::named(
                     v, [](std::string_view s) { return canvas::line_dash_from_string(s); },
                     [](canvas::LineDash d) { return canvas::to_string(d); });
                 if (!dash) {
                     return EditResult::fail("invalid line dash");
                 }
                 o.line.dash = *dash;
                 return EditResult::success();
             }},
    Property{"stroke_width",
             [](Object& o, const json& v) {
                 const auto n = pv::number(v);
                 if (!n) {
                     return EditResult::fail("invalid border width");
                 }
                 o.line.width = k_size_range.clamp(*n);
                 return EditResult::success();
             },
             k_size_range},
    // Effects: each field by its own name, so a toggle or a field writes one.
    Property{"shadow_on", [](Object& o, const json& v) { return set_flag_field(o.effects.shadow.on, v); }},
    Property{"shadow_color",
             [](Object& o, const json& v) { return set_color_field(o.effects.shadow.color, v); }},
    Property{"shadow_blur",
             [](Object& o, const json& v) { return set_size_field(o.effects.shadow.blur, v); },
             k_size_range},
    Property{"shadow_distance",
             [](Object& o, const json& v) { return set_size_field(o.effects.shadow.distance, v); },
             k_size_range},
    Property{"shadow_angle",
             [](Object& o, const json& v) { return set_angle_field(o.effects.shadow.angle, v); }},
    Property{"glow_on", [](Object& o, const json& v) { return set_flag_field(o.effects.glow.on, v); }},
    Property{"glow_color",
             [](Object& o, const json& v) { return set_color_field(o.effects.glow.color, v); }},
    Property{"glow_size",
             [](Object& o, const json& v) { return set_size_field(o.effects.glow.size, v); },
             k_size_range},
    Property{"soft_edge", [](Object& o, const json& v) { return set_size_field(o.effects.soft_edge, v); },
             k_size_range},
    Property{"text_outline_color",
             [](Object& o, const json& v) {
                 return set_color_field(o.effects.text_outline.color, v);
             }},
    Property{"text_outline_width",
             [](Object& o, const json& v) {
                 return set_size_field(o.effects.text_outline.width, v);
             },
             k_size_range},
    Property{"text_shadow_on",
             [](Object& o, const json& v) { return set_flag_field(o.effects.text_shadow.on, v); }},
    Property{"text_shadow_color",
             [](Object& o, const json& v) {
                 return set_color_field(o.effects.text_shadow.color, v);
             }},
    Property{"text_shadow_blur",
             [](Object& o, const json& v) { return set_size_field(o.effects.text_shadow.blur, v); },
             k_size_range},
    Property{"text_shadow_distance",
             [](Object& o, const json& v) {
                 return set_size_field(o.effects.text_shadow.distance, v);
             },
             k_size_range},
    Property{"text_shadow_angle",
             [](Object& o, const json& v) {
                 return set_angle_field(o.effects.text_shadow.angle, v);
             }},
    // A picture's adjustments.
    Property{"crop_left", [](Object& o, const json& v) { return set_crop_field(o.picture.crop.left, v); },
             k_crop_range},
    Property{"crop_top", [](Object& o, const json& v) { return set_crop_field(o.picture.crop.top, v); },
             k_crop_range},
    Property{"crop_right",
             [](Object& o, const json& v) { return set_crop_field(o.picture.crop.right, v); },
             k_crop_range},
    Property{"crop_bottom",
             [](Object& o, const json& v) { return set_crop_field(o.picture.crop.bottom, v); },
             k_crop_range},
    Property{"recolor",
             [](Object& o, const json& v) {
                 const auto recolor = pv::named(
                     v, [](std::string_view s) { return canvas::recolor_from_string(s); },
                     [](canvas::Recolor r) { return canvas::to_string(r); });
                 if (!recolor) {
                     return EditResult::fail("invalid recolor");
                 }
                 o.picture.recolor = *recolor;
                 return EditResult::success();
             }},
    Property{"tint", [](Object& o, const json& v) { return set_color_field(o.picture.tint, v); }},
    Property{"picture_transparency",
             [](Object& o, const json& v) {
                 const auto n = pv::number(v);
                 if (!n) {
                     return EditResult::fail("expected a transparency");
                 }
                 o.picture.transparency = k_share_range.clamp(*n);
                 return EditResult::success();
             },
             k_share_range},
    // A video's.
    Property{"video_start",
             [](Object& o, const json& v) {
                 const auto start = pv::named(
                     v, [](std::string_view s) { return canvas::video_start_from_string(s); },
                     [](canvas::VideoStart s) { return canvas::to_string(s); });
                 if (!start) {
                     return EditResult::fail("invalid video start");
                 }
                 o.video.start = *start;
                 return EditResult::success();
             }},
    Property{"video_loop", [](Object& o, const json& v) { return set_flag_field(o.video.loop, v); }},
    Property{"video_mute", [](Object& o, const json& v) { return set_flag_field(o.video.mute, v); }},
    Property{"video_poster",
             [](Object& o, const json& v) {
                 auto s = pv::text(v);
                 if (!s) {
                     return EditResult::fail("expected a picture");
                 }
                 o.video.poster = std::move(*s);
                 return EditResult::success();
             }},
    // Paragraphs.
    Property{"bullets",
             [](Object& o, const json& v) {
                 const auto bullets = pv::named(
                     v, [](std::string_view s) { return canvas::bullets_from_string(s); },
                     [](canvas::Bullets b) { return canvas::to_string(b); });
                 if (!bullets) {
                     return EditResult::fail("invalid bullets");
                 }
                 o.paragraph.bullets = *bullets;
                 return EditResult::success();
             }},
    Property{"space_before",
             [](Object& o, const json& v) {
                 // The lines' own value goes only once the box's is taken.
                 auto set = set_size_field(o.paragraph.space_before, v);
                 if (set) {
                     clear_line_formats(o, [](model::LineFormat& f) { f.before_pct = -1; });
                 }
                 return set;
             },
             k_size_range},
    Property{"space_after",
             [](Object& o, const json& v) {
                 // The lines' own value goes only once the box's is taken.
                 auto set = set_size_field(o.paragraph.space_after, v);
                 if (set) {
                     clear_line_formats(o, [](model::LineFormat& f) { f.after_pct = -1; });
                 }
                 return set;
             },
             k_size_range},
    Property{"indent",
             [](Object& o, const json& v) {
                 // The lines' own value goes only once the box's is taken.
                 auto set = set_size_field(o.paragraph.indent, v);
                 if (set) {
                     clear_line_formats(o, [](model::LineFormat& f) { f.indent_pct = -1; });
                 }
                 return set;
             },
             k_size_range},
    Property{"autofit",
             [](Object& o, const json& v) {
                 const auto autofit = pv::named(
                     v, [](std::string_view s) { return canvas::autofit_from_string(s); },
                     [](canvas::Autofit a) { return canvas::to_string(a); });
                 if (!autofit) {
                     return EditResult::fail("invalid autofit");
                 }
                 o.paragraph.autofit = *autofit;
                 return EditResult::success();
             }},
    Property{"columns",
             [](Object& o, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a number of columns");
                 }
                 o.paragraph.columns = k_columns_range.clamp(*n);
                 return EditResult::success();
             },
             k_columns_range},
    Property{"column_gap",
             [](Object& o, const json& v) { return set_size_field(o.paragraph.column_gap, v); },
             k_size_range},
    Property{"margin_left",
             [](Object& o, const json& v) { return set_size_field(o.paragraph.margins.left, v); },
             k_size_range},
    Property{"margin_top",
             [](Object& o, const json& v) { return set_size_field(o.paragraph.margins.top, v); },
             k_size_range},
    Property{"margin_right",
             [](Object& o, const json& v) { return set_size_field(o.paragraph.margins.right, v); },
             k_size_range},
    Property{"margin_bottom",
             [](Object& o, const json& v) {
                 return set_size_field(o.paragraph.margins.bottom, v);
             },
             k_size_range},
    Property{"shape_kind",
             [](Object& o, const json& v) {
                 const auto kind = pv::named(
                     v, [](std::string_view s) { return geom::shape_kind_from_string(s); },
                     [](geom::ShapeKind k) { return geom::to_string(k); });
                 if (!kind) {
                     return EditResult::fail("unknown shape");
                 }
                 // The adjustments are re-clamped into the new kind's window.
                 o.shape_kind = *kind;
                 o.shape_adjust = geom::clamp_adjust(o.shape_kind, o.shape_adjust);
                 return EditResult::success();
             }},
    Property{"shape_corner",
             [](Object& o, const json& v) {
                 return set_adjust(o, v, [](geom::ShapeAdjust& a, double n) { a.corner = n; });
             },
             k_corner_range},
    Property{"shape_points",
             [](Object& o, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a number of points");
                 }
                 auto adjust = o.shape_adjust;
                 adjust.points = *n;
                 o.shape_adjust = geom::clamp_adjust(o.shape_kind, adjust);
                 return EditResult::success();
             },
             k_points_range},
    Property{"shape_inner",
             [](Object& o, const json& v) {
                 return set_adjust(o, v, [](geom::ShapeAdjust& a, double n) { a.inner = n; });
             },
             k_inner_range},
    Property{"shape_head",
             [](Object& o, const json& v) {
                 return set_adjust(o, v, [](geom::ShapeAdjust& a, double n) { a.head = n; });
             },
             k_head_range},
    Property{"shape_thickness",
             [](Object& o, const json& v) {
                 return set_adjust(o, v, [](geom::ShapeAdjust& a, double n) { a.thickness = n; });
             },
             k_thickness_range},
    // A table's.
    Property{"table_rows",
             [](Object& o, const json& v) { return set_table_size(o.table, true, v); },
             k_table_side_range},
    Property{"table_columns",
             [](Object& o, const json& v) { return set_table_size(o.table, false, v); },
             k_table_side_range},
    Property{"table_header", [](Object& o, const json& v) { return set_flag_field(o.table.header, v); }},
    Property{"table_banding", [](Object& o, const json& v) { return set_flag_field(o.table.banding, v); }},
    Property{"table_border_color",
             [](Object& o, const json& v) { return set_color_field(o.table.border_color, v); }},
    Property{"table_border_width",
             [](Object& o, const json& v) {
                 const auto n = pv::number(v);
                 if (!n) {
                     return EditResult::fail("expected a width");
                 }
                 o.table.border_width = k_table_border_range.clamp(*n);
                 return EditResult::success();
             },
             k_table_border_range},
    Property{"table_cells",
             [](Object& o, const json& v) {
                 if (!v.is_array()) {
                     return EditResult::fail("expected the cells");
                 }
                 const auto count = static_cast<std::size_t>(o.table.rows) * static_cast<std::size_t>(o.table.columns);
                 std::vector<std::string> cells(count);
                 for (std::size_t i = 0; i < count && i < v.size(); ++i) {
                     if (v[i].is_string()) {
                         cells[i] = v[i].get<std::string>();
                     }
                 }
                 o.table.cells = std::move(cells);
                 return EditResult::success();
             }},
    Property{"shape_path",
             [](Object& o, const json& v) {
                 if (!v.is_array()) {
                     return EditResult::fail("expected points");
                 }
                 std::vector<geom::PathPoint> path;
                 for (const auto& entry : v) {
                     auto point = canvas::path_point_from_json(entry);
                     if (!point) {
                         return EditResult::fail("expected points");
                     }
                     point->x = std::clamp(point->x, 0.0, 1.0);
                     point->y = std::clamp(point->y, 0.0, 1.0);
                     path.push_back(*point);
                 }
                 o.shape_adjust.path = std::move(path);
                 return EditResult::success();
             }},
    Property{"shape_closed",
             [](Object& o, const json& v) {
                 const bool was = o.shape_adjust.closed;
                 auto result = set_flag_field(o.shape_adjust.closed, v);
                 if (result && o.shape_adjust.closed != was) {
                     // Joining up changes the curve at the ends.
                     fit_drawn_box(o);
                 }
                 return result;
             }},
    // An open outline's ends.
    Property{"line_start", [](Object& o, const json& v) { return set_line_end(o.line.start, v); }},
    Property{"line_end", [](Object& o, const json& v) { return set_line_end(o.line.end, v); }},
    Property{"indicator_fallback",
             [](Object& o, const json& v) {
                 const auto fallback = pv::named(
                     v, [](std::string_view s) { return indicator_fallback_from_string(s); },
                     [](IndicatorFallback f) { return to_string(f); });
                 if (!fallback) {
                     return EditResult::fail("invalid indicator fallback");
                 }
                 o.indicator_fallback = *fallback;
                 return EditResult::success();
             }},
    Property{"indicator_gap",
             [](Object& o, const json& v) {
                 const auto n = pv::number(v);
                 if (!n) {
                     return EditResult::fail("expected an indicator gap");
                 }
                 o.indicator_gap = clamp_indicator_gap(*n);
                 return EditResult::success();
             },
             k_indicator_gap_range},
    Property{"clock_24h", [](Object& o, const json& v) { return set_flag_field(o.clock_24h, v); }},
    Property{"clock_seconds",
             [](Object& o, const json& v) { return set_flag_field(o.clock_seconds, v); }},
    Property{"upcoming_color",
             [](Object& o, const json& v) { return set_color_field(o.upcoming_color, v); }},
    Property{"show_chords", [](Object& o, const json& v) { return set_flag_field(o.show_chords, v); }},
    Property{"chord_size_pct",
             [](Object& o, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a chord size");
                 }
                 o.chords.size_pct = k_chord_size_range.clamp(*n);
                 return EditResult::success();
             },
             k_chord_size_range},
    Property{"chord_gap_pct",
             [](Object& o, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a chord gap");
                 }
                 o.chords.gap_pct = k_chord_gap_range.clamp(*n);
                 return EditResult::success();
             },
             k_chord_gap_range},
    Property{"chord_color",
             [](Object& o, const json& v) { return set_color_field(o.chords.color, v); }},
    Property{"upcoming_font_size",
             [](Object& o, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a font size");
                 }
                 // 0 follows the box's own size.
                 o.upcoming_font_size = *n <= 0 ? 0 : k_font_size_range.clamp(*n);
                 return EditResult::success();
             },
             k_font_size_range},
    Property{"upcoming_bold",
             [](Object& o, const json& v) {
                 return set_upcoming_flag(o.upcoming_bold, o.bold, v);
             }},
    Property{"upcoming_italic",
             [](Object& o, const json& v) {
                 return set_upcoming_flag(o.upcoming_italic, o.italic, v);
             }},
    Property{"upcoming_underline",
             [](Object& o, const json& v) {
                 return set_upcoming_flag(o.upcoming_underline, o.underline, v);
             }},
};

}  // namespace

namespace {

/// A drawn shape's points as the sidebar and the stage read them: each
/// [x, y, smooth], shares of the box.
[[nodiscard]] json drawn_points(const geom::ShapeAdjust& adjust)
{
    auto out = json::array();
    for (const auto& pt : adjust.path) {
        out.push_back(json::array({pt.x, pt.y, pt.smooth ? 1 : 0}));
    }
    return out;
}

}  // namespace

EditResult set_flag_field(bool& field, const json& v)
{
    const auto on = pv::flag(v);
    if (!on) {
        return EditResult::fail("expected on or off");
    }
    field = *on;
    return EditResult::success();
}

EditResult set_color_field(std::string& field, const json& v)
{
    auto s = pv::text(v);
    if (!s || s->empty()) {
        return EditResult::fail("color required");
    }
    field = std::move(*s);
    return EditResult::success();
}

EditResult set_object_property(Object& obj, std::string_view name, const nlohmann::json& value)
{
    for (const auto& property : k_properties) {
        if (property.name == name) {
            return property.apply(obj, value);
        }
    }
    return EditResult::fail("unknown property " + std::string(name));
}

json object_property_ranges(const Object& obj)
{
    json out = json::object();
    for (const auto& property : k_properties) {
        if (property.range) {
            Range range = *property.range;
            if (property.name == "shape_head") {
                range.max = geom::head_max(obj.shape_kind);
            }
            out[std::string(property.name)] = range;
        }
    }
    return out;
}

json object_property_values(const Object& obj)
{
    const canvas::BoxType type = painted_type(obj);
    const geom::ShapeAdjust adjust = geom::clamp_adjust(obj.shape_kind, obj.shape_adjust);
    // Halves round up, as the ribbon's own Math.round did.
    const auto px = [](double v) { return static_cast<int>(std::floor(v + 0.5)); };
    return {
        {"x", px(obj.x)},
        {"y", px(obj.y)},
        {"width", px(obj.w)},
        {"height", px(obj.h)},
        {"rotation", obj.rotation},
        {"font_family", type.font_family},
        {"font_size", type.font_size},
        {"font_color", type.font_color},
        {"bold", obj.bold},
        {"italic", obj.italic},
        {"underline", obj.underline},
        {"align", type.text_align},
        {"valign", type.vertical_align},
        {"line_height", type.line_height},
        {"line_gap", obj.line_gap},
        {"letter_spacing", obj.letter_spacing},
        {"opacity", obj.opacity},
        {"object_fit", painted_object_fit(obj)},
        {"filename", obj.filename},
        {"fill", obj.fill.color},
        // What paints: a solid fill with no colour reads as none; a picture
        // fill reads as one even before its picture is chosen.
        {"fill_kind", obj.fill.kind == canvas::FillKind::Picture || canvas::fill_paints(obj.fill)
                          ? std::string(canvas::to_string(obj.fill.kind))
                          : std::string("none")},
        {"gradient_kind", std::string(canvas::to_string(obj.fill.gradient.kind))},
        {"gradient_angle", obj.fill.gradient.angle},
        {"gradient_center_x", obj.fill.gradient.center_x},
        {"gradient_center_y", obj.fill.gradient.center_y},
        {"gradient_from", obj.fill.gradient.stops.empty() ? std::string() : obj.fill.gradient.stops.front().color},
        {"gradient_to", obj.fill.gradient.stops.size() < 2 ? std::string() : obj.fill.gradient.stops.back().color},
        {"fill_picture_file", obj.fill.picture.file},
        {"fill_picture_fit", obj.fill.picture.fit},
        {"crop_left", obj.picture.crop.left},
        {"crop_top", obj.picture.crop.top},
        {"crop_right", obj.picture.crop.right},
        {"crop_bottom", obj.picture.crop.bottom},
        {"recolor", std::string(canvas::to_string(obj.picture.recolor))},
        {"tint", obj.picture.tint},
        {"picture_transparency", obj.picture.transparency},
        {"video_start", std::string(canvas::to_string(obj.video.start))},
        {"video_loop", obj.video.loop},
        {"video_mute", obj.video.mute},
        {"video_poster", obj.video.poster},
        {"stroke", obj.line.color},
        {"stroke_width", obj.line.width},
        {"line_dash", std::string(canvas::to_string(obj.line.dash))},
        {"shadow_on", obj.effects.shadow.on},
        {"shadow_color", obj.effects.shadow.color},
        {"shadow_blur", obj.effects.shadow.blur},
        {"shadow_distance", obj.effects.shadow.distance},
        {"shadow_angle", obj.effects.shadow.angle},
        {"glow_on", obj.effects.glow.on},
        {"glow_color", obj.effects.glow.color},
        {"glow_size", obj.effects.glow.size},
        {"soft_edge", obj.effects.soft_edge},
        {"text_outline_color", obj.effects.text_outline.color},
        {"text_outline_width", obj.effects.text_outline.width},
        {"text_shadow_on", obj.effects.text_shadow.on},
        {"text_shadow_color", obj.effects.text_shadow.color},
        {"text_shadow_blur", obj.effects.text_shadow.blur},
        {"text_shadow_distance", obj.effects.text_shadow.distance},
        {"text_shadow_angle", obj.effects.text_shadow.angle},
        {"bullets", std::string(canvas::to_string(obj.paragraph.bullets))},
        {"space_before", obj.paragraph.space_before},
        {"space_after", obj.paragraph.space_after},
        {"indent", obj.paragraph.indent},
        {"autofit", std::string(canvas::to_string(obj.paragraph.autofit))},
        {"columns", obj.paragraph.columns},
        {"column_gap", obj.paragraph.column_gap},
        {"margin_left", obj.paragraph.margins.left},
        {"margin_top", obj.paragraph.margins.top},
        {"margin_right", obj.paragraph.margins.right},
        {"margin_bottom", obj.paragraph.margins.bottom},
        {"shape_kind", std::string(geom::to_string(obj.shape_kind))},
        {"shape_corner", adjust.corner},
        {"shape_points", adjust.points},
        {"shape_inner", adjust.inner},
        {"shape_head", adjust.head},
        {"shape_thickness", adjust.thickness},
        {"table_rows", obj.table.rows},
        {"table_columns", obj.table.columns},
        {"table_header", obj.table.header},
        {"table_banding", obj.table.banding},
        {"table_border_color", obj.table.border_color},
        {"table_border_width", obj.table.border_width},
        {"table_cells", obj.table.cells},
        {"shape_path", drawn_points(adjust)},
        {"shape_closed", adjust.closed},
        {"line_start", std::string(canvas::to_string(obj.line.start))},
        {"line_end", std::string(canvas::to_string(obj.line.end))},
        {"indicator_fallback", std::string(to_string(obj.indicator_fallback))},
        {"indicator_gap", obj.indicator_gap},
        {"clock_24h", obj.clock_24h},
        {"clock_seconds", obj.clock_seconds},
        {"upcoming_color", obj.upcoming_color.empty() ? std::string(k_default_upcoming_color)
                                                      : obj.upcoming_color},
        {"upcoming_font_size",
         obj.upcoming_font_size > 0 ? obj.upcoming_font_size : type.font_size},
        {"upcoming_bold", obj.upcoming_bold.value_or(obj.bold)},
        {"upcoming_italic", obj.upcoming_italic.value_or(obj.italic)},
        {"upcoming_underline", obj.upcoming_underline.value_or(obj.underline)},
        {"show_chords", obj.show_chords},
        {"chord_size_pct", obj.chords.size_pct},
        {"chord_gap_pct", obj.chords.gap_pct},
        {"chord_color", obj.chords.color},
    };
}

}  // namespace seventhslide::freeform
