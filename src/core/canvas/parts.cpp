#include "seventhslide/canvas/parts.hpp"

#include "named.hpp"

#include "seventhslide/json/read.hpp"

#include <algorithm>

namespace seventhslide::canvas {
namespace {

using seventhslide::json::bool_or;
using seventhslide::json::int_or;
using seventhslide::json::num_or;
using seventhslide::json::str_or;

constexpr Named<AnimationEffect> k_animation_effects[] = {
    {AnimationEffect::Fade, "fade"},          {AnimationEffect::Appear, "appear"},
    {AnimationEffect::FlyIn, "fly_in"},       {AnimationEffect::Wipe, "wipe"},
    {AnimationEffect::Zoom, "zoom"},          {AnimationEffect::Float, "float"},
    {AnimationEffect::Pulse, "pulse"},        {AnimationEffect::Grow, "grow"},
    {AnimationEffect::Disappear, "disappear"}, {AnimationEffect::FadeOut, "fade_out"},
    {AnimationEffect::FlyOut, "fly_out"},     {AnimationEffect::WipeOut, "wipe_out"},
    {AnimationEffect::ZoomOut, "zoom_out"},   {AnimationEffect::FloatOut, "float_out"},
};
constexpr Named<AnimationTrigger> k_animation_triggers[] = {
    {AnimationTrigger::OnClick, "on_click"},
    {AnimationTrigger::WithPrevious, "with_previous"},
    {AnimationTrigger::AfterPrevious, "after_previous"},
};

constexpr Named<FillKind> k_fill_kinds[] = {
    {FillKind::Solid, "solid"},
    {FillKind::None, "none"},
    {FillKind::Gradient, "gradient"},
    {FillKind::Picture, "picture"},
};
constexpr Named<GradientKind> k_gradient_kinds[] = {
    {GradientKind::Linear, "linear"},
    {GradientKind::Radial, "radial"},
};
constexpr Named<LineDash> k_dashes[] = {
    {LineDash::Solid, "solid"},
    {LineDash::Dash, "dash"},
    {LineDash::Dot, "dot"},
    {LineDash::DashDot, "dash_dot"},
};
constexpr Named<LineEnd> k_line_ends[] = {
    {LineEnd::None, "none"},
    {LineEnd::Arrow, "arrow"},
    {LineEnd::Dot, "dot"},
};
constexpr Named<Bullets> k_bullets[] = {
    {Bullets::None, "none"},
    {Bullets::Disc, "disc"},
    {Bullets::Number, "number"},
};
constexpr Named<Autofit> k_autofits[] = {
    {Autofit::None, "none"},
    {Autofit::Shrink, "shrink"},
};
constexpr Named<Recolor> k_recolors[] = {
    {Recolor::None, "none"},
    {Recolor::Grayscale, "grayscale"},
    {Recolor::Tint, "tint"},
};
constexpr Named<VideoStart> k_video_starts[] = {
    {VideoStart::OnShow, "on_show"},
    {VideoStart::OnClick, "on_click"},
};
}  // namespace

std::string_view to_string(FillKind kind) noexcept
{
    return name_of(k_fill_kinds, kind);
}

FillKind fill_kind_from_string(std::string_view s) noexcept
{
    return value_of(k_fill_kinds, s);
}

bool fill_paints(const Fill& fill) noexcept
{
    switch (fill.kind) {
    case FillKind::None:
        return false;
    case FillKind::Solid:
        return !fill.color.empty();
    case FillKind::Gradient:
        return fill.gradient.stops.size() >= 2;
    case FillKind::Picture:
        return !fill.picture.file.empty();
    }
    return false;
}

std::string_view to_string(LineDash dash) noexcept
{
    return name_of(k_dashes, dash);
}

LineDash line_dash_from_string(std::string_view s) noexcept
{
    return value_of(k_dashes, s);
}

bool line_paints(const Line& line) noexcept
{
    return !line.color.empty() && line.width > 0.0;
}

double effects_reach(const Effects& effects) noexcept
{
    // A blur spreads about twice its radius before it fades to nothing.
    double reach = 0.0;
    for (const Shadow* shadow : {&effects.shadow, &effects.text_shadow}) {
        if (shadow->on) {
            reach = std::max(reach, shadow->distance + shadow->blur * 2.0);
        }
    }
    if (effects.glow.on) {
        reach = std::max(reach, effects.glow.size * 2.0);
    }
    if (effects.text_outline.width > 0.0 && !effects.text_outline.color.empty()) {
        reach = std::max(reach, effects.text_outline.width);
    }
    return reach;
}

std::string_view to_string(Bullets bullets) noexcept
{
    return name_of(k_bullets, bullets);
}

Bullets bullets_from_string(std::string_view s) noexcept
{
    return value_of(k_bullets, s);
}

std::string_view to_string(Autofit autofit) noexcept
{
    return name_of(k_autofits, autofit);
}

Autofit autofit_from_string(std::string_view s) noexcept
{
    return value_of(k_autofits, s);
}

std::string_view to_string(GradientKind kind) noexcept
{
    return name_of(k_gradient_kinds, kind);
}

GradientKind gradient_kind_from_string(std::string_view s) noexcept
{
    return value_of(k_gradient_kinds, s);
}

std::string_view to_string(VideoStart start) noexcept
{
    return name_of(k_video_starts, start);
}

VideoStart video_start_from_string(std::string_view s) noexcept
{
    return value_of(k_video_starts, s);
}

std::string_view to_string(Recolor recolor) noexcept
{
    return name_of(k_recolors, recolor);
}

Recolor recolor_from_string(std::string_view s) noexcept
{
    return value_of(k_recolors, s);
}

std::string_view to_string(LineEnd end) noexcept
{
    return name_of(k_line_ends, end);
}

LineEnd line_end_from_string(std::string_view s) noexcept
{
    return value_of(k_line_ends, s);
}

// --- the parts' stored forms --------------------------------------------
// In the namespace of the parts, so nlohmann::json's conversions find them.

void to_json(nlohmann::json& j, const Crop& v)
{
    j = {{"left", v.left}, {"top", v.top}, {"right", v.right}, {"bottom", v.bottom}};
}

void from_json(const nlohmann::json& j, Crop& v)
{
    const Crop d;
    v.left = std::clamp(num_or(j, "left", d.left), 0.0, 1.0);
    v.top = std::clamp(num_or(j, "top", d.top), 0.0, 1.0);
    v.right = std::clamp(num_or(j, "right", d.right), 0.0, 1.0);
    v.bottom = std::clamp(num_or(j, "bottom", d.bottom), 0.0, 1.0);
}

void to_json(nlohmann::json& j, const Gradient& v)
{
    j = {{"kind", std::string(name_of(k_gradient_kinds, v.kind))}, {"angle", v.angle}};
    if (v.center_x != Gradient{}.center_x || v.center_y != Gradient{}.center_y) {
        j["center_x"] = v.center_x;
        j["center_y"] = v.center_y;
    }
    auto stops = nlohmann::json::array();
    for (const auto& stop : v.stops) {
        stops.push_back({{"at", stop.at}, {"color", stop.color}});
    }
    j["stops"] = std::move(stops);
}

void from_json(const nlohmann::json& j, Gradient& v)
{
    const Gradient d;
    v.kind = enum_or(j, "kind", k_gradient_kinds, d.kind);
    v.angle = num_or(j, "angle", d.angle);
    v.center_x = std::clamp(num_or(j, "center_x", d.center_x), 0.0, 1.0);
    v.center_y = std::clamp(num_or(j, "center_y", d.center_y), 0.0, 1.0);
    v.stops.clear();
    if (const auto* stops = json::find(j, "stops"); stops != nullptr && stops->is_array()) {
        for (const auto& s : *stops) {
            v.stops.push_back({.at = std::clamp(num_or(s, "at", 0.0), 0.0, 1.0),
                               .color = str_or(s, "color")});
        }
    }
}

void to_json(nlohmann::json& j, const PictureFill& v)
{
    j = {{"file", v.file}, {"fit", v.fit}};
    if (v.crop != Crop{}) {
        j["crop"] = v.crop;
    }
}

void from_json(const nlohmann::json& j, PictureFill& v)
{
    const PictureFill d;
    v.file = str_or(j, "file");
    v.fit = str_or(j, "fit", d.fit);
    v.crop = {};
    if (const auto* crop = json::find(j, "crop"); crop != nullptr && crop->is_object()) {
        v.crop = crop->get<Crop>();
    }
}

void to_json(nlohmann::json& j, const Shadow& v)
{
    j = {{"on", v.on},
         {"color", v.color},
         {"blur", v.blur},
         {"distance", v.distance},
         {"angle", v.angle}};
}

void from_json(const nlohmann::json& j, Shadow& v)
{
    const Shadow d;
    v.on = bool_or(j, "on", d.on);
    v.color = str_or(j, "color", d.color);
    v.blur = std::max(0.0, num_or(j, "blur", d.blur));
    v.distance = std::max(0.0, num_or(j, "distance", d.distance));
    v.angle = num_or(j, "angle", d.angle);
}

void to_json(nlohmann::json& j, const Effects& v)
{
    j = nlohmann::json::object();
    if (v.shadow != Shadow{}) {
        j["shadow"] = v.shadow;
    }
    if (v.glow != Glow{}) {
        j["glow"] = {{"on", v.glow.on}, {"color", v.glow.color}, {"size", v.glow.size}};
    }
    if (v.soft_edge != 0) {
        j["soft_edge"] = v.soft_edge;
    }
    if (v.text_outline != TextOutline{}) {
        j["text_outline"] = {{"color", v.text_outline.color}, {"width", v.text_outline.width}};
    }
    if (v.text_shadow != Shadow{}) {
        j["text_shadow"] = v.text_shadow;
    }
}

void from_json(const nlohmann::json& j, Effects& v)
{
    v = {};
    if (const auto* s = json::find(j, "shadow"); s != nullptr && s->is_object()) {
        v.shadow = s->get<Shadow>();
    }
    if (const auto* g = json::find(j, "glow"); g != nullptr && g->is_object()) {
        const Glow d;
        v.glow.on = bool_or(*g, "on", d.on);
        v.glow.color = str_or(*g, "color", d.color);
        v.glow.size = std::max(0.0, num_or(*g, "size", d.size));
    }
    v.soft_edge = std::max(0.0, num_or(j, "soft_edge", 0.0));
    if (const auto* o = json::find(j, "text_outline"); o != nullptr && o->is_object()) {
        const TextOutline d;
        v.text_outline.color = str_or(*o, "color", d.color);
        v.text_outline.width = std::max(0.0, num_or(*o, "width", d.width));
    }
    if (const auto* s = json::find(j, "text_shadow"); s != nullptr && s->is_object()) {
        v.text_shadow = s->get<Shadow>();
    }
}

void to_json(nlohmann::json& j, const Paragraph& v)
{
    const Paragraph d;
    j = nlohmann::json::object();
    if (v.bullets != d.bullets) {
        j["bullets"] = std::string(name_of(k_bullets, v.bullets));
    }
    if (v.space_before != d.space_before) {
        j["space_before"] = v.space_before;
    }
    if (v.space_after != d.space_after) {
        j["space_after"] = v.space_after;
    }
    if (v.indent != d.indent) {
        j["indent"] = v.indent;
    }
    if (v.autofit != d.autofit) {
        j["autofit"] = std::string(name_of(k_autofits, v.autofit));
    }
    if (v.margins != d.margins) {
        j["margins"] = {{"left", v.margins.left},
                        {"top", v.margins.top},
                        {"right", v.margins.right},
                        {"bottom", v.margins.bottom}};
    }
    if (v.columns != d.columns) {
        j["columns"] = v.columns;
    }
    if (v.column_gap != d.column_gap) {
        j["column_gap"] = v.column_gap;
    }
}

void from_json(const nlohmann::json& j, Paragraph& v)
{
    const Paragraph d;
    v.bullets = enum_or(j, "bullets", k_bullets, d.bullets);
    v.space_before = std::max(0.0, num_or(j, "space_before", d.space_before));
    v.space_after = std::max(0.0, num_or(j, "space_after", d.space_after));
    v.indent = num_or(j, "indent", d.indent);
    v.autofit = enum_or(j, "autofit", k_autofits, d.autofit);
    v.margins = {};
    if (const auto* m = json::find(j, "margins"); m != nullptr && m->is_object()) {
        v.margins.left = std::max(0.0, num_or(*m, "left", 0.0));
        v.margins.top = std::max(0.0, num_or(*m, "top", 0.0));
        v.margins.right = std::max(0.0, num_or(*m, "right", 0.0));
        v.margins.bottom = std::max(0.0, num_or(*m, "bottom", 0.0));
    }
    v.columns = std::clamp(int_or(j, "columns", d.columns), 1, 8);
    v.column_gap = std::max(0.0, num_or(j, "column_gap", d.column_gap));
}

void to_json(nlohmann::json& j, const Picture& v)
{
    const Picture d;
    j = nlohmann::json::object();
    if (v.crop != d.crop) {
        j["crop"] = v.crop;
    }
    if (v.recolor != d.recolor) {
        j["recolor"] = std::string(name_of(k_recolors, v.recolor));
    }
    if (!v.tint.empty()) {
        j["tint"] = v.tint;
    }
    if (v.transparency != d.transparency) {
        j["transparency"] = v.transparency;
    }
}

void from_json(const nlohmann::json& j, Picture& v)
{
    const Picture d;
    v = {};
    if (const auto* crop = json::find(j, "crop"); crop != nullptr && crop->is_object()) {
        v.crop = crop->get<Crop>();
    }
    v.recolor = enum_or(j, "recolor", k_recolors, d.recolor);
    v.tint = str_or(j, "tint");
    v.transparency = std::clamp(num_or(j, "transparency", d.transparency), 0.0, 1.0);
}

void to_json(nlohmann::json& j, const Video& v)
{
    j = {{"start", std::string(name_of(k_video_starts, v.start))},
         {"loop", v.loop},
         {"mute", v.mute}};
    if (!v.poster.empty()) {
        j["poster"] = v.poster;
    }
}

void from_json(const nlohmann::json& j, Video& v)
{
    const Video d;
    v.start = enum_or(j, "start", k_video_starts, d.start);
    v.loop = bool_or(j, "loop", d.loop);
    v.mute = bool_or(j, "mute", d.mute);
    v.poster = str_or(j, "poster");
}

void to_json(nlohmann::json& j, const Table& v)
{
    j = {{"rows", v.rows},
         {"columns", v.columns},
         {"cells", v.cells},
         {"border_color", v.border_color},
         {"border_width", v.border_width},
         {"header", v.header},
         {"banding", v.banding}};
}

void from_json(const nlohmann::json& j, Table& v)
{
    const Table d;
    v = {};
    v.rows = std::clamp(int_or(j, "rows", 0), 0, 100);
    v.columns = std::clamp(int_or(j, "columns", 0), 0, 50);
    if (const auto* cells = json::find(j, "cells"); cells != nullptr && cells->is_array()) {
        for (const auto& c : *cells) {
            v.cells.push_back(c.is_string() ? c.get<std::string>() : std::string{});
        }
    }
    v.cells.resize(static_cast<std::size_t>(v.rows) * static_cast<std::size_t>(v.columns));
    v.border_color = str_or(j, "border_color", d.border_color);
    v.border_width = std::max(0.0, num_or(j, "border_width", d.border_width));
    v.header = bool_or(j, "header", d.header);
    v.banding = bool_or(j, "banding", d.banding);
}

void to_json(nlohmann::json& j, const LineType& v)
{
    j = nlohmann::json::object();
    if (!v.family.empty()) {
        j["family"] = v.family;
    }
    if (v.size > 0) {
        j["size"] = v.size;
    }
    if (v.bold) {
        j["bold"] = *v.bold;
    }
    if (v.italic) {
        j["italic"] = *v.italic;
    }
    if (v.underline) {
        j["underline"] = *v.underline;
    }
    if (!v.color.empty()) {
        j["color"] = v.color;
    }
}

void from_json(const nlohmann::json& j, LineType& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    v.family = str_or(j, "family");
    v.size = std::max(0, int_or(j, "size", 0));
    const auto flag = [&j](const char* key) -> std::optional<bool> {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_boolean()) {
            return std::nullopt;
        }
        return it->get<bool>();
    };
    v.bold = flag("bold");
    v.italic = flag("italic");
    v.underline = flag("underline");
    v.color = str_or(j, "color");
}

std::string_view to_string(AnimationEffect effect) noexcept
{
    return name_of(k_animation_effects, effect);
}

AnimationEffect animation_effect_from_string(std::string_view s) noexcept
{
    return value_of(k_animation_effects, s);
}

std::string_view to_string(AnimationTrigger trigger) noexcept
{
    return name_of(k_animation_triggers, trigger);
}

AnimationTrigger animation_trigger_from_string(std::string_view s) noexcept
{
    return value_of(k_animation_triggers, s);
}

void to_json(nlohmann::json& j, const Animation& v)
{
    j = nlohmann::json{{"order", v.order},
                       {"effect", std::string(to_string(v.effect))},
                       {"trigger", std::string(to_string(v.trigger))},
                       {"duration_ms", v.duration_ms}};
    if (v.delay_ms > 0) {
        j["delay_ms"] = v.delay_ms;
    }
    if (animation_has_direction(v.effect)) {
        j["direction"] = std::string(model::to_string(v.direction));
    }
}

void from_json(const nlohmann::json& j, Animation& v)
{
    v = {};
    if (!j.is_object()) {
        return;
    }
    v.order = int_or(j, "order", 0);
    v.effect = enum_or(j, "effect", k_animation_effects, v.effect);
    v.trigger = enum_or(j, "trigger", k_animation_triggers, v.trigger);
    v.duration_ms = std::clamp(int_or(j, "duration_ms", v.duration_ms), 0, 60'000);
    v.delay_ms = std::clamp(int_or(j, "delay_ms", 0), 0, 60'000);
    v.direction = model::transition_direction_from_string(str_or(j, "direction", "left"));
}

}  // namespace seventhslide::canvas
