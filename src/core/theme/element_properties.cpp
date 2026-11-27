#include "seventhslide/theme/element_properties.hpp"

#include "seventhslide/freeform/object_properties.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace seventhslide::theme {
namespace {

namespace pv = freeform::property_value;
using freeform::EditResult;
using freeform::set_color_field;
using freeform::set_flag_field;
using json = nlohmann::json;

using Apply = EditResult (*)(Element&, const json&);
using canvas::Range;

/// One property: its name, its writer and, for a number held to a span, the
/// span its writer clamps to.
struct Property {
    std::string_view name;
    Apply apply;
    std::optional<Range> range = std::nullopt;
};

// The spans the writers below clamp to.
constexpr Range k_count_range{1, 99};
constexpr Range k_chord_size_range{layout::k_min_chord_size_pct, layout::k_max_chord_size_pct};
constexpr Range k_chord_gap_range{0, layout::k_max_chord_gap_pct};
/// Verse numbers may be set smaller than a box's words.
constexpr Range k_verse_size_range{1, freeform::k_max_font_size};
constexpr Range k_verse_margin_range{0, 400};
/// A line type's own size; 0 hands the size back to the box.
constexpr Range k_line_size_range{8, freeform::k_max_font_size};

constexpr std::array k_properties = {
    Property{"label",
             [](Element& el, const json& v) {
                 auto s = pv::text(v);
                 if (!s) {
                     return EditResult::fail("expected a label");
                 }
                 el.label = std::move(*s);
                 return EditResult::success();
             }},
    Property{"rule",
             [](Element& el, const json& v) {
                 const auto rule = pv::named(
                     v, [](std::string_view s) { return slide_rule_from_string(s); },
                     [](SlideRule r) { return to_string(r); });
                 if (!rule) {
                     return EditResult::fail("invalid slide rule");
                 }
                 el.visibility.rule = *rule;
                 return EditResult::success();
             }},
    Property{"count",
             [](Element& el, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a slide count");
                 }
                 el.visibility.count = k_count_range.clamp(*n);
                 return EditResult::success();
             },
             k_count_range},
    Property{"show_when",
             [](Element& el, const json& v) {
                 const auto when = pv::named(
                     v, [](std::string_view s) { return show_when_from_string(s); },
                     [](ShowWhen w) { return to_string(w); });
                 if (!when) {
                     return EditResult::fail("invalid show-when");
                 }
                 el.show_when = *when;
                 return EditResult::success();
             }},
    Property{"show_chords",
             [](Element& el, const json& v) { return set_flag_field(el.body.show_chords, v); }},
    Property{"chord_size_pct",
             [](Element& el, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a chord size");
                 }
                 el.body.chords.size_pct = k_chord_size_range.clamp(*n);
                 return EditResult::success();
             },
             k_chord_size_range},
    Property{"chord_gap_pct",
             [](Element& el, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a chord gap");
                 }
                 el.body.chords.gap_pct = k_chord_gap_range.clamp(*n);
                 return EditResult::success();
             },
             k_chord_gap_range},
    Property{"chord_color",
             [](Element& el, const json& v) { return set_color_field(el.body.chords.color, v); }},
    Property{"verse_numbers",
             [](Element& el, const json& v) { return set_flag_field(el.body.verse_numbers, v); }},
    Property{"verse_font_family",
             [](Element& el, const json& v) {
                 auto s = pv::text(v);
                 if (!s || s->empty()) {
                     return EditResult::fail("font required");
                 }
                 el.body.verse_font_family = std::move(*s);
                 return EditResult::success();
             }},
    Property{"verse_font_size",
             [](Element& el, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a font size");
                 }
                 el.body.verse_font_size = k_verse_size_range.clamp(*n);
                 return EditResult::success();
             },
             k_verse_size_range},
    Property{"verse_font_bold",
             [](Element& el, const json& v) { return set_flag_field(el.body.verse_font_bold, v); }},
    Property{"verse_font_italic",
             [](Element& el, const json& v) { return set_flag_field(el.body.verse_font_italic, v); }},
    Property{"verse_color",
             [](Element& el, const json& v) { return set_color_field(el.body.verse_color, v); }},
    Property{"verse_valign",
             [](Element& el, const json& v) {
                 auto s = pv::text(v);
                 if (!s || (*s != "super" && *s != "middle" && *s != "baseline")) {
                     return EditResult::fail("invalid verse number position");
                 }
                 el.body.verse_valign = std::move(*s);
                 return EditResult::success();
             }},
    Property{"verse_margin_pct",
             [](Element& el, const json& v) {
                 const auto n = pv::whole(v);
                 if (!n) {
                     return EditResult::fail("expected a margin");
                 }
                 el.body.verse_margin_right_pct = k_verse_margin_range.clamp(*n);
                 return EditResult::success();
             },
             k_verse_margin_range},
};

/// The lines' types are properties under ``active_line_`` and ``other_lines_``:
/// family, size, bold, italic, underline, color. An empty family, a size of 0,
/// an empty colour or a null flag hands the field back to the box.
constexpr std::string_view k_active_line_prefix = "active_line_";
constexpr std::string_view k_other_lines_prefix = "other_lines_";

EditResult set_line_type_field(canvas::LineType& type, std::string_view field, const json& v)
{
    if (field == "family" || field == "color") {
        auto s = pv::text(v);
        if (!s) {
            return EditResult::fail(field == "family" ? "expected a font" : "expected a color");
        }
        (field == "family" ? type.family : type.color) = std::move(*s);
        return EditResult::success();
    }
    if (field == "size") {
        const auto n = pv::whole(v);
        if (!n) {
            return EditResult::fail("expected a font size");
        }
        type.size = *n <= 0 ? 0 : k_line_size_range.clamp(*n);
        return EditResult::success();
    }
    if (field == "bold" || field == "italic" || field == "underline") {
        std::optional<bool> flag;
        if (!v.is_null()) {
            flag = pv::flag(v);
            if (!flag) {
                return EditResult::fail("expected on or off");
            }
        }
        (field == "bold" ? type.bold : field == "italic" ? type.italic : type.underline) = flag;
        return EditResult::success();
    }
    return EditResult::fail("unknown property");
}

[[nodiscard]] std::pair<canvas::LineType*, std::string_view> line_type_field(Element& el,
                                                                            std::string_view name)
{
    if (name.starts_with(k_active_line_prefix)) {
        return {&el.body.active_line, name.substr(k_active_line_prefix.size())};
    }
    if (name.starts_with(k_other_lines_prefix)) {
        return {&el.body.other_lines, name.substr(k_other_lines_prefix.size())};
    }
    return {nullptr, {}};
}

void line_type_values(json& values, std::string_view prefix, const canvas::LineType& type)
{
    const auto key = [prefix](std::string_view field) {
        return std::string(prefix) + std::string(field);
    };
    const auto flag = [](const std::optional<bool>& f) { return f ? json(*f) : json(nullptr); };
    values[key("family")] = type.family;
    values[key("size")] = type.size;
    values[key("bold")] = flag(type.bold);
    values[key("italic")] = flag(type.italic);
    values[key("underline")] = flag(type.underline);
    values[key("color")] = type.color;
}

}  // namespace

EditResult set_element_property(Element& el, std::string_view name, const nlohmann::json& value)
{
    for (const auto& property : k_properties) {
        if (property.name == name) {
            return property.apply(el, value);
        }
    }
    if (const auto [type, field] = line_type_field(el, name); type != nullptr) {
        return set_line_type_field(*type, field, value);
    }
    return freeform::set_object_property(el.object, name, value);
}

json element_property_ranges(const Element& el)
{
    json out = freeform::object_property_ranges(el.object);
    for (const auto& property : k_properties) {
        if (property.range) {
            out[std::string(property.name)] = *property.range;
        }
    }
    for (const auto prefix : {k_active_line_prefix, k_other_lines_prefix}) {
        out[std::string(prefix) + "size"] = k_line_size_range;
    }
    return out;
}

json element_property_values(const Element& el)
{
    json values = freeform::object_property_values(el.object);
    values["label"] = el.label;
    values["rule"] = std::string(to_string(el.visibility.rule));
    values["count"] = el.visibility.count;
    values["show_when"] = std::string(to_string(el.show_when));
    line_type_values(values, k_active_line_prefix, el.body.active_line);
    line_type_values(values, k_other_lines_prefix, el.body.other_lines);
    values["show_chords"] = el.body.show_chords;
    values["chord_size_pct"] = el.body.chords.size_pct;
    values["chord_gap_pct"] = el.body.chords.gap_pct;
    values["chord_color"] = el.body.chords.color;
    values["verse_numbers"] = el.body.verse_numbers;
    values["verse_font_family"] = el.body.verse_font_family;
    values["verse_font_size"] = el.body.verse_font_size;
    values["verse_font_bold"] = el.body.verse_font_bold;
    values["verse_font_italic"] = el.body.verse_font_italic;
    values["verse_color"] = el.body.verse_color;
    values["verse_valign"] = el.body.verse_valign;
    values["verse_margin_pct"] = el.body.verse_margin_right_pct;
    return values;
}

}  // namespace seventhslide::theme
