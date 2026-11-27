#include "seventhslide/freeform/paint.hpp"

#include "seventhslide/geom/shape_path.hpp"

#include <string>

namespace seventhslide::freeform {
namespace {

[[nodiscard]] std::string or_default(const std::string& value, const char* fallback)
{
    return value.empty() ? std::string(fallback) : value;
}

}  // namespace

canvas::TypeDefaults type_defaults(ObjectType type) noexcept
{
    switch (type) {
    case ObjectType::Clock:
        return {.text_align = "left", .vertical_align = "top"};
    case ObjectType::Indicator:
        return {.font_size = 30, .text_align = "left", .vertical_align = "top"};
    case ObjectType::Text:
    case ObjectType::Image:
    case ObjectType::Video:
    case ObjectType::Shape:
    case ObjectType::Table:
        break;
    }
    return {};
}

void fill_painted_type(canvas::BoxType& type, ObjectType kind)
{
    canvas::fill_type_defaults(type, type_defaults(kind));
    type.line_height = layout::clamp_overlay_line_height(type.line_height);
}

canvas::BoxType painted_type(const Object& obj)
{
    canvas::BoxType type = obj;
    fill_painted_type(type, obj.type);
    return type;
}

std::string painted_object_fit(const Object& obj)
{
    return or_default(obj.object_fit, "contain");
}

canvas::Object painted_object(const Object& obj)
{
    canvas::Object out = obj;
    canvas::fill_type_defaults(out, type_defaults(obj.type));
    out.object_fit = painted_object_fit(obj);
    out.shape_adjust = geom::clamp_adjust(obj.shape_kind, obj.shape_adjust);
    return out;
}

layout::OverlayBox indicator_box(const Object& obj)
{
    layout::OverlayBox box;
    static_cast<canvas::BoxRect&>(box) = obj;
    static_cast<canvas::BoxType&>(box) = obj;
    box.rotation = obj.rotation;
    fill_painted_type(box, obj.type);
    box.effects = obj.effects;
    return box;
}

}  // namespace seventhslide::freeform
