#include "seventhslide/canvas/object.hpp"

#include "named.hpp"

#include "seventhslide/json/read.hpp"
#include "seventhslide/model/static_media.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace seventhslide::canvas {
namespace {

using seventhslide::json::bool_or;
using seventhslide::json::int_or;
using seventhslide::json::num_or;
using seventhslide::json::str_or;

constexpr Named<ObjectType> k_object_types[] = {
    {ObjectType::Text, "text"},       {ObjectType::Image, "image"},
    {ObjectType::Video, "video"},     {ObjectType::Shape, "shape"},
    {ObjectType::Table, "table"},     {ObjectType::Indicator, "indicator"},
    {ObjectType::Clock, "clock"},
};
constexpr Named<IndicatorFallback> k_fallbacks[] = {
    {IndicatorFallback::Slide, "slide"},
    {IndicatorFallback::Blank, "blank"},
};
constexpr Named<ClockMode> k_clock_modes[] = {
    {ClockMode::WallClock, "wall_clock"},
    {ClockMode::Countdown, "countdown"},
};

void write_typeface(nlohmann::json& j, const BoxType& t)
{
    j["font_family"] = t.font_family;
    j["font_size"] = t.font_size;
    j["font_color"] = t.font_color;
    j["text_align"] = t.text_align;
    j["vertical_align"] = t.vertical_align;
}


/// The geometry names the ribbon sends, and the field each writes.
struct GeometryField {
    std::string_view name;
    double canvas::BoxFrame::*member;
};

constexpr std::array k_geometry_fields = {
    GeometryField{"x", &canvas::BoxFrame::x},
    GeometryField{"y", &canvas::BoxFrame::y},
    GeometryField{"width", &canvas::BoxFrame::w},
    GeometryField{"height", &canvas::BoxFrame::h},
    GeometryField{"rotation", &canvas::BoxFrame::rotation},
};

[[nodiscard]] const GeometryField* geometry_field(std::string_view name) noexcept
{
    for (const auto& field : k_geometry_fields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

}  // namespace

std::string_view to_string(ObjectType type) noexcept
{
    return name_of(k_object_types, type);
}

ObjectType object_type_from_string(std::string_view s) noexcept
{
    return value_of(k_object_types, s);
}

bool paints_frame(const Object& obj) noexcept
{
    switch (obj.type) {
    case ObjectType::Text:
    case ObjectType::Indicator:
    case ObjectType::Clock:
        return fill_paints(obj.fill) || line_paints(obj.line);
    case ObjectType::Image:
    case ObjectType::Video:
    case ObjectType::Shape:
    case ObjectType::Table:
        return false;
    }
    return false;
}

std::string_view to_string(IndicatorFallback fallback) noexcept
{
    return name_of(k_fallbacks, fallback);
}

IndicatorFallback indicator_fallback_from_string(std::string_view s) noexcept
{
    return value_of(k_fallbacks, s);
}

double clamp_indicator_gap(double gap) noexcept
{
    if (std::isnan(gap)) {
        return k_default_indicator_gap;
    }
    return std::clamp(gap, k_min_indicator_gap, k_max_indicator_gap);
}

std::string read_vertical_align(std::string_view value)
{
    if (value.empty() || value == "top" || value == "middle" || value == "bottom") {
        return std::string(value);
    }
    return "middle";
}

std::optional<geom::PathPoint> path_point_from_json(const nlohmann::json& j)
{
    if (!j.is_array() || j.size() < 2 || !j[0].is_number() || !j[1].is_number()) {
        return std::nullopt;
    }
    return geom::PathPoint{.x = j[0].get<double>(),
                           .y = j[1].get<double>(),
                           .smooth = j.size() > 2 && j[2].is_number() && j[2].get<double>() != 0};
}

std::optional<std::string> object_media_url(const Object& obj)
{
    switch (obj.type) {
    case ObjectType::Image:
        return model::media_ref_url(model::k_images_subdir, obj.filename);
    case ObjectType::Video:
        return model::media_ref_url(model::k_videos_subdir, obj.filename);
    case ObjectType::Text:
    case ObjectType::Shape:
    case ObjectType::Table:
    case ObjectType::Indicator:
    case ObjectType::Clock:
        break;
    }
    return std::nullopt;
}

std::optional<std::string> fill_picture_url(const Fill& fill)
{
    if (fill.kind != FillKind::Picture || fill.picture.file.empty()) {
        return std::nullopt;
    }
    return model::media_ref_url(model::k_images_subdir, fill.picture.file);
}

void map_object(Object& obj, const geom::CanvasMapping& mapping)
{
    if (mapping.is_identity()) {
        return;
    }
    obj.x = mapping.x(obj.x);
    obj.y = mapping.y(obj.y);
    obj.w = mapping.width(obj.w);
    obj.h = mapping.height(obj.h);
    obj.font_size = mapping.font_size(obj.font_size);
    obj.line_gap = mapping.pixels(obj.line_gap);
    obj.letter_spacing = mapping.length(obj.letter_spacing);
    if (!obj.line.color.empty()) {
        obj.line.width = mapping.length(obj.line.width);
    }
    for (Shadow* shadow : {&obj.effects.shadow, &obj.effects.text_shadow}) {
        if (shadow->on) {
            shadow->blur = mapping.length(shadow->blur);
            shadow->distance = mapping.length(shadow->distance);
        }
    }
    if (obj.effects.glow.on) {
        obj.effects.glow.size = mapping.length(obj.effects.glow.size);
    }
    obj.effects.soft_edge = mapping.length(obj.effects.soft_edge);
    obj.effects.text_outline.width = mapping.length(obj.effects.text_outline.width);
    obj.paragraph.space_before = mapping.length(obj.paragraph.space_before);
    obj.paragraph.space_after = mapping.length(obj.paragraph.space_after);
    obj.paragraph.indent = mapping.length(obj.paragraph.indent);
    obj.paragraph.margins.left = mapping.length(obj.paragraph.margins.left);
    obj.paragraph.margins.top = mapping.length(obj.paragraph.margins.top);
    obj.paragraph.margins.right = mapping.length(obj.paragraph.margins.right);
    obj.paragraph.margins.bottom = mapping.length(obj.paragraph.margins.bottom);
    obj.paragraph.column_gap = mapping.length(obj.paragraph.column_gap);
    if (obj.type == ObjectType::Table) {
        obj.table.border_width = mapping.length(obj.table.border_width);
    }
}

void map_object_json(nlohmann::json& obj, const geom::CanvasMapping& mapping)
{
    if (!obj.is_object() || mapping.is_identity()) {
        return;
    }
    // The same fields as map_object, with the same rounding.
    const auto update = [](nlohmann::json& holder, const char* key, auto&& fn) {
        if (!holder.is_object()) {
            return;
        }
        const auto it = holder.find(key);
        if (it != holder.end() && it->is_number()) {
            *it = fn(it->template get<double>());
        }
    };
    const auto whole = [](double v) { return static_cast<int>(std::lround(v)); };
    const auto length = [&](double v) { return mapping.length(v); };
    update(obj, "x", [&](double v) { return mapping.x(v); });
    update(obj, "y", [&](double v) { return mapping.y(v); });
    update(obj, "w", [&](double v) { return mapping.width(v); });
    update(obj, "h", [&](double v) { return mapping.height(v); });
    update(obj, "font_size", [&](double v) { return mapping.font_size(whole(v)); });
    update(obj, "line_gap", [&](double v) { return mapping.pixels(whole(v)); });
    update(obj, "letter_spacing", length);
    // A shape always stores its width; it maps only when the line paints.
    if (const auto* stroke = json::find(obj, "stroke");
        stroke != nullptr && stroke->is_string() && !stroke->get_ref<const std::string&>().empty()) {
        update(obj, "stroke_width", length);
    }
    const auto is_on = [](const nlohmann::json& part) {
        const auto* on = json::find(part, "on");
        return on != nullptr && on->is_boolean() && on->get<bool>();
    };
    if (auto* effects = json::member(obj, "effects")) {
        for (const char* shadow : {"shadow", "text_shadow"}) {
            if (auto* s = json::member(*effects, shadow); s != nullptr && is_on(*s)) {
                update(*s, "blur", length);
                update(*s, "distance", length);
            }
        }
        if (auto* glow = json::member(*effects, "glow"); glow != nullptr && is_on(*glow)) {
            update(*glow, "size", length);
        }
        update(*effects, "soft_edge", length);
        if (auto* outline = json::member(*effects, "text_outline")) {
            update(*outline, "width", length);
        }
    }
    if (auto* paragraph = json::member(obj, "paragraph")) {
        for (const char* key : {"space_before", "space_after", "indent", "column_gap"}) {
            update(*paragraph, key, length);
        }
        if (auto* margins = json::member(*paragraph, "margins")) {
            for (const char* key : {"left", "top", "right", "bottom"}) {
                update(*margins, key, length);
            }
        }
    }
    if (auto* table = json::member(obj, "table")) {
        update(*table, "border_width", length);
    }
}

bool keeps_proportions(ObjectType type) noexcept
{
    switch (type) {
    case ObjectType::Image:
    case ObjectType::Video:
    case ObjectType::Shape:
        return true;
    case ObjectType::Text:
    case ObjectType::Table:
    case ObjectType::Indicator:
    case ObjectType::Clock:
        break;
    }
    return false;
}

BoxRect resized_frame(const BoxRect& before, geom::CanvasSize from, geom::CanvasSize to)
{
    const geom::CanvasSize f = geom::canvas_or_reference(from);
    const geom::CanvasSize t = to.valid() ? to : f;
    const double sx = static_cast<double>(t.width) / f.width;
    const double sy = static_cast<double>(t.height) / f.height;
    const BoxRect per_axis{before.x * sx, before.y * sy, before.w * sx, before.h * sy};

    // Half a pixel either way is "at the edge".
    constexpr double eps = 0.5;
    const bool covers = before.x <= eps && before.y <= eps && before.x + before.w >= f.width - eps
                        && before.y + before.h >= f.height - eps;
    if (covers || sx == sy || before.w <= 0 || before.h <= 0) {
        return per_axis;
    }

    const double mean = std::sqrt(sx * sy);
    double w = before.w * mean;
    double h = before.h * mean;
    const double pull = std::min({1.0, t.width / w, t.height / h});
    w *= pull;
    h *= pull;
    double x = (before.x + before.w / 2) * sx - w / 2;
    double y = (before.y + before.h / 2) * sy - h / 2;
    const bool was_inside = before.x >= -eps && before.y >= -eps
                            && before.x + before.w <= f.width + eps
                            && before.y + before.h <= f.height + eps;
    if (was_inside) {
        x = std::clamp(x, 0.0, std::max(0.0, t.width - w));
        y = std::clamp(y, 0.0, std::max(0.0, t.height - h));
    }
    return {x, y, w, h};
}

void keep_proportions(Object& obj, const BoxRect& before, geom::CanvasSize from,
                      geom::CanvasSize to)
{
    if (!keeps_proportions(obj.type)) {
        return;
    }
    static_cast<BoxRect&>(obj) = resized_frame(before, from, to);
}

BoxRect frame_of_json(const nlohmann::json& obj)
{
    BoxRect frame;
    frame.x = json::num_or(obj, "x", 0.0);
    frame.y = json::num_or(obj, "y", 0.0);
    frame.w = json::num_or(obj, "w", 0.0);
    frame.h = json::num_or(obj, "h", 0.0);
    return frame;
}

void keep_proportions_json(nlohmann::json& obj, const BoxRect& before, geom::CanvasSize from,
                           geom::CanvasSize to)
{
    if (!obj.is_object()
        || !keeps_proportions(object_type_from_string(json::str_or(obj, "type", "")))) {
        return;
    }
    const BoxRect frame = resized_frame(before, from, to);
    obj["x"] = frame.x;
    obj["y"] = frame.y;
    obj["w"] = frame.w;
    obj["h"] = frame.h;
}

void resize_object(Object& obj, geom::CanvasSize from, geom::CanvasSize to)
{
    const BoxRect before = obj;
    map_object(obj, geom::CanvasMapping::resize(from, to));
    keep_proportions(obj, before, from, to);
}

void resize_object_json(nlohmann::json& obj, geom::CanvasSize from, geom::CanvasSize to)
{
    if (!obj.is_object()) {
        return;
    }
    const BoxRect before = frame_of_json(obj);
    map_object_json(obj, geom::CanvasMapping::resize(from, to));
    keep_proportions_json(obj, before, from, to);
}

void to_json(nlohmann::json& j, const Object& v)
{
    j = nlohmann::json{
        {"id", v.id},
        {"type", std::string(to_string(v.type))},
        {"x", v.x},
        {"y", v.y},
        {"w", v.w},
        {"h", v.h},
        {"rotation", v.rotation},
        {"opacity", v.opacity},
        {"z", v.z},
    };
    if (v.flip_h) {
        j["flip_h"] = true;
    }
    if (v.flip_v) {
        j["flip_v"] = true;
    }
    if (v.locked) {
        j["locked"] = true;
    }
    if (!v.group_id.empty()) {
        j["group_id"] = v.group_id;
    }
    switch (v.type) {
    case ObjectType::Text:
    case ObjectType::Table:
        j["text"] = v.text;
        write_typeface(j, v);
        j["bold"] = v.bold;
        j["italic"] = v.italic;
        j["underline"] = v.underline;
        j["line_height"] = v.line_height;
        j["line_gap"] = v.line_gap;
        j["letter_spacing"] = v.letter_spacing;
        if (v.type == ObjectType::Table) {
            j["table"] = v.table;
        }
        break;
    case ObjectType::Indicator:
        write_typeface(j, v);
        j["indicator_fallback"] = std::string(to_string(v.indicator_fallback));
        if (v.indicator_gap != k_default_indicator_gap) {
            j["indicator_gap"] = v.indicator_gap;
        }
        break;
    case ObjectType::Clock:
        write_typeface(j, v);
        // Weight and slant paint, so they have to survive a save.
        j["bold"] = v.bold;
        j["italic"] = v.italic;
        j["clock_24h"] = v.clock_24h;
        j["clock_seconds"] = v.clock_seconds;
        if (v.clock_mode != ClockMode::WallClock) {
            j["clock_mode"] = std::string(name_of(k_clock_modes, v.clock_mode));
        }
        break;
    case ObjectType::Image:
    case ObjectType::Video:
        j["filename"] = v.filename;
        j["object_fit"] = v.object_fit;
        if (v.type == ObjectType::Video) {
            j["video"] = v.video;
        } else if (v.picture != Picture{}) {
            j["picture"] = v.picture;
        }
        break;
    case ObjectType::Shape: {
        j["shape_kind"] = std::string(geom::to_string(v.shape_kind));
        // Adjustments the kind ignores are dropped so the JSON stays readable
        // and preview keys do not churn on values nothing paints.
        if (geom::uses_corner(v.shape_kind)) {
            j["shape_corner"] = v.shape_adjust.corner;
        }
        if (geom::uses_points(v.shape_kind)) {
            j["shape_points"] = v.shape_adjust.points;
        }
        if (geom::uses_inner(v.shape_kind)) {
            j["shape_inner"] = v.shape_adjust.inner;
        }
        if (geom::uses_arrow(v.shape_kind)) {
            j["shape_head"] = v.shape_adjust.head;
            j["shape_thickness"] = v.shape_adjust.thickness;
        }
        if (geom::uses_path(v.shape_kind)) {
            // Each point [x, y], or [x, y, 1] when smooth.
            auto points = nlohmann::json::array();
            for (const auto& pt : v.shape_adjust.path) {
                auto entry = nlohmann::json::array({pt.x, pt.y});
                if (pt.smooth) {
                    entry.push_back(1);
                }
                points.push_back(std::move(entry));
            }
            j["shape_path"] = std::move(points);
            if (v.shape_adjust.closed) {
                j["shape_closed"] = true;
            }
        }
        break;
    }
    }
    // A shape's fill and outline have always been stored; another object's
    // only when it has one.
    const Fill default_fill;
    const Line default_line;
    const bool shape = v.type == ObjectType::Shape;
    if (shape || !v.fill.color.empty()) {
        j["fill"] = v.fill.color;
    }
    if (shape || !v.line.color.empty()) {
        j["stroke"] = v.line.color;
        j["stroke_width"] = v.line.width;
    }
    if (v.fill.kind != default_fill.kind) {
        j["fill_kind"] = std::string(to_string(v.fill.kind));
    }
    if (v.fill.gradient != default_fill.gradient) {
        j["gradient"] = v.fill.gradient;
    }
    if (v.fill.picture != default_fill.picture) {
        j["fill_picture"] = v.fill.picture;
    }
    if (v.line.dash != default_line.dash) {
        j["line_dash"] = std::string(to_string(v.line.dash));
    }
    if (v.line.start != default_line.start) {
        j["line_start"] = std::string(to_string(v.line.start));
    }
    if (v.line.end != default_line.end) {
        j["line_end"] = std::string(to_string(v.line.end));
    }
    if (v.effects != Effects{}) {
        j["effects"] = v.effects;
    }
    if (v.paragraph != Paragraph{}) {
        j["paragraph"] = v.paragraph;
    }
    // A plain cut is what an object without one has always meant.
    if (v.transition != model::Transition{}) {
        j["transition"] = v.transition;
    }
}

void from_json(const nlohmann::json& j, Object& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    v.id = str_or(j, "id");
    v.type = object_type_from_string(str_or(j, "type", "text"));
    v.x = num_or(j, "x", 0);
    v.y = num_or(j, "y", 0);
    v.w = num_or(j, "w", 100);
    v.h = num_or(j, "h", 100);
    v.rotation = num_or(j, "rotation", 0);
    v.opacity = num_or(j, "opacity", 1);
    v.z = int_or(j, "z", 0);
    v.group_id = str_or(j, "group_id");
    v.locked = bool_or(j, "locked", false);
    v.flip_h = bool_or(j, "flip_h", false);
    v.flip_v = bool_or(j, "flip_v", false);
    v.text = str_or(j, "text");
    v.font_family = str_or(j, "font_family", k_default_font_family);
    v.font_size = int_or(j, "font_size", k_default_font_size);
    v.font_color = str_or(j, "font_color", k_default_font_color);
    v.bold = bool_or(j, "bold", false);
    v.italic = bool_or(j, "italic", false);
    v.underline = bool_or(j, "underline", false);
    v.text_align = str_or(j, "text_align", k_default_text_align);
    v.vertical_align = read_vertical_align(str_or(j, "vertical_align", k_default_vertical_align));
    v.line_height = num_or(j, "line_height", k_default_line_height);
    v.line_gap = int_or(j, "line_gap", 0);
    v.letter_spacing = num_or(j, "letter_spacing", 0);
    v.filename = str_or(j, "filename");
    v.object_fit = str_or(j, "object_fit", "contain");
    v.shape_kind = geom::shape_kind_from_string(str_or(j, "shape_kind", "rectangle"));
    const geom::ShapeAdjust adj_defaults;
    geom::ShapeAdjust adj;
    adj.corner = num_or(j, "shape_corner", adj_defaults.corner);
    adj.points = int_or(j, "shape_points", adj_defaults.points);
    adj.inner = num_or(j, "shape_inner", adj_defaults.inner);
    adj.head = num_or(j, "shape_head", adj_defaults.head);
    adj.thickness = num_or(j, "shape_thickness", adj_defaults.thickness);
    if (const auto* points = json::member(j, "shape_path"); points != nullptr && points->is_array()) {
        for (const auto& entry : *points) {
            if (const auto point = path_point_from_json(entry)) {
                adj.path.push_back(*point);
            }
        }
    }
    adj.closed = bool_or(j, "shape_closed", false);
    v.shape_adjust = geom::clamp_adjust(v.shape_kind, adj);
    const Fill default_fill;
    const Line default_line;
    // A stored shape without them has a new shape's colours.
    const bool shape = v.type == ObjectType::Shape;
    v.fill.kind = fill_kind_from_string(str_or(j, "fill_kind", std::string(to_string(default_fill.kind))));
    v.fill.color = str_or(j, "fill", shape ? std::string(k_default_shape_fill) : std::string{});
    if (const auto* g = json::find(j, "gradient"); g != nullptr && g->is_object()) {
        v.fill.gradient = g->get<Gradient>();
    }
    if (const auto* p = json::find(j, "fill_picture"); p != nullptr && p->is_object()) {
        v.fill.picture = p->get<PictureFill>();
    }
    v.line.color =
        str_or(j, "stroke", shape ? std::string(k_default_shape_stroke) : std::string{});
    v.line.width = std::clamp(num_or(j, "stroke_width", default_line.width), 0.0, k_max_line_width);
    v.line.dash = line_dash_from_string(str_or(j, "line_dash", std::string(to_string(default_line.dash))));
    v.line.start = line_end_from_string(str_or(j, "line_start", std::string(to_string(default_line.start))));
    v.line.end = line_end_from_string(str_or(j, "line_end", std::string(to_string(default_line.end))));
    if (const auto* e = json::find(j, "effects"); e != nullptr && e->is_object()) {
        v.effects = e->get<Effects>();
    }
    if (const auto* p = json::find(j, "paragraph"); p != nullptr && p->is_object()) {
        v.paragraph = p->get<Paragraph>();
    }
    if (const auto* p = json::find(j, "picture"); p != nullptr && p->is_object()) {
        v.picture = p->get<Picture>();
    }
    if (const auto* vid = json::find(j, "video"); vid != nullptr && vid->is_object()) {
        v.video = vid->get<Video>();
    }
    if (const auto* t = json::find(j, "table"); t != nullptr && t->is_object()) {
        v.table = t->get<Table>();
    }
    v.indicator_fallback = indicator_fallback_from_string(str_or(j, "indicator_fallback", "slide"));
    v.indicator_gap = clamp_indicator_gap(num_or(j, "indicator_gap", k_default_indicator_gap));
    v.clock_24h = bool_or(j, "clock_24h", false);
    v.clock_seconds = bool_or(j, "clock_seconds", false);
    v.clock_mode = enum_or(j, "clock_mode", k_clock_modes, ClockMode::WallClock);
    if (const auto* tr = json::find(j, "transition")) {
        v.transition = model::optional_transition_from_json(*tr).value_or(model::Transition{});
    }
}

bool is_geometry_property(std::string_view name) noexcept
{
    return geometry_field(name) != nullptr;
}

std::optional<canvas::BoxFrame> with_geometry_property(canvas::BoxFrame frame,
                                                       std::string_view name,
                                                       double value) noexcept
{
    const auto* field = geometry_field(name);
    if (field == nullptr) {
        return std::nullopt;
    }
    frame.*(field->member) =
        field->member == &canvas::BoxFrame::rotation ? normalized_rotation(value) : value;
    return frame;
}

double normalized_rotation(double degrees) noexcept
{
    if (!std::isfinite(degrees)) {
        return 0.0;
    }
    const double wrapped = std::fmod(degrees, 360.0);
    return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}

}  // namespace seventhslide::canvas
