#include "seventhslide/layout/background_fill.hpp"

#include "seventhslide/json/read.hpp"

namespace seventhslide::layout {

std::string_view to_string(BackgroundType type) noexcept
{
    switch (type) {
    case BackgroundType::Transparent:
        break;
    case BackgroundType::Color:
        return "color";
    case BackgroundType::Image:
        return "image";
    case BackgroundType::Video:
        return "video";
    }
    return "transparent";
}

BackgroundType background_type_from_string(std::string_view type) noexcept
{
    if (type == "color") {
        return BackgroundType::Color;
    }
    if (type == "image") {
        return BackgroundType::Image;
    }
    if (type == "video") {
        return BackgroundType::Video;
    }
    return BackgroundType::Transparent;
}

std::optional<BackgroundFill> background_fill_from_style(const nlohmann::json& style)
{
    if (!style.is_object()) {
        return std::nullopt;
    }
    using seventhslide::json::str_or;
    BackgroundFill fill;
    // A style that does not name a type lets the stack underneath show through.
    fill.type = background_type_from_string(str_or(style, "background_type"));
    fill.color = str_or(style, "background_color", fill.color);
    // An empty color string is a real value for a video poster (no plate).
    // A color fill has to paint something; the painters already treat a
    // missing hex as black, so store the black they will paint.
    if (fill.type == BackgroundType::Color && fill.color.empty()) {
        fill.color = k_default_fill_color;
    }
    if (fill.type == BackgroundType::Image) {
        fill.image = str_or(style, "background_image", "");
    }
    if (fill.type == BackgroundType::Video) {
        fill.video = str_or(style, "background_video", "");
    }
    return fill;
}

BackgroundFade background_fade_from_style(const nlohmann::json& style)
{
    BackgroundFade fade;
    fade.enabled = json::bool_or(style, "bg_enable_fade", fade.enabled);
    fade.duration_ms = json::style_int(style, "bg_fade_duration", fade.duration_ms);
    return fade;
}

}  // namespace seventhslide::layout
