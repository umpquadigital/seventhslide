#include "seventhslide/freeform/background_copy.hpp"

#include "seventhslide/layout/background_fill.hpp"

#include <utility>

namespace seventhslide::freeform {

BackgroundCopy background_copy_of(const layout::BackgroundFill& fill, model::Transition transition)
{
    BackgroundCopy out;
    out.transition = transition;
    switch (fill.type) {
    case layout::BackgroundType::Transparent:
        break;
    case layout::BackgroundType::Color:
        out.background.kind = SlideBackgroundKind::Color;
        out.background.color = fill.color;
        break;
    case layout::BackgroundType::Image:
        if (fill.image.empty()) {
            break;
        }
        out.background.kind = SlideBackgroundKind::Image;
        out.copy_from_url = fill.image;
        break;
    case layout::BackgroundType::Video:
        if (fill.video.empty()) {
            break;
        }
        out.background.kind = SlideBackgroundKind::Video;
        out.copy_from_url = fill.video;
        break;
    }
    return out;
}

BackgroundCopy background_copy_of(const nlohmann::json& bg_style)
{
    const auto fade = layout::background_fade_from_style(bg_style);
    return background_copy_of(
        layout::background_fill_from_style(bg_style).value_or(layout::BackgroundFill{}),
        model::transition_from_fade(fade.enabled, fade.duration_ms));
}

void place_copied_file(BackgroundCopy& copy, std::string filename)
{
    if (copy.background.kind == SlideBackgroundKind::Video) {
        copy.background.video = std::move(filename);
    } else {
        copy.background.image = std::move(filename);
    }
    copy.copy_from_url.clear();
}

}  // namespace seventhslide::freeform
