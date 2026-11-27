#include "seventhslide/freeform/background_copy.hpp"

#include "seventhslide/layout/background_fill.hpp"
#include "seventhslide/model/static_media.hpp"

#include <string_view>
#include <utility>

namespace seventhslide::freeform {
namespace {

/// ``url``'s file when it is in the library's ``subdir`` folder, which a
/// slide can name as it is; empty for anything else.
[[nodiscard]] std::string library_file(std::string_view url, std::string_view subdir)
{
    const std::string prefix = "/static/" + std::string(subdir) + "/";
    if (url.starts_with(prefix)) {
        return std::string(model::media_basename(url));
    }
    return {};
}

}  // namespace

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
        out.background.image = library_file(fill.image, model::k_images_subdir);
        if (out.background.image.empty()) {
            out.copy_from_url = fill.image;
        }
        break;
    case layout::BackgroundType::Video:
        if (fill.video.empty()) {
            break;
        }
        out.background.kind = SlideBackgroundKind::Video;
        out.background.video = library_file(fill.video, model::k_videos_subdir);
        if (out.background.video.empty()) {
            out.copy_from_url = fill.video;
        }
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
