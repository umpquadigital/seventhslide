#include <seventhslide/scene/scene_frame.hpp>

#include <cmath>
#include <string>

namespace seventhslide::scene {

std::string_view to_string(SceneMode mode) noexcept
{
    switch (mode) {
    case SceneMode::Empty:
        return "empty";
    case SceneMode::Song:
        return "song";
    case SceneMode::Bible:
        return "bible";
    case SceneMode::Freeform:
        return "freeform";
    case SceneMode::Image:
        return "image";
    case SceneMode::Video:
        return "video";
    case SceneMode::Audio:
        return "audio";
    case SceneMode::Web:
        return "web";
    case SceneMode::Live:
        return "live";
    }
    return "empty";
}

std::string web_source_key(std::string_view url, int width, int height, double zoom)
{
    // Whole percent: the dialog offers no finer zoom, and a key that differs
    // in the tenth decimal would start a second renderer for the same page.
    const auto percent = static_cast<long>(std::lround(zoom * 100.0));
    return std::to_string(width) + "x" + std::to_string(height) + "@" + std::to_string(percent)
           + " " + std::string(url);
}

std::string web_tab_key(std::uint64_t tab)
{
    return "tab " + std::to_string(tab);
}

std::string video_object_key(std::string_view url)
{
    return "video " + std::string(url);
}

std::string content_video_key(std::uint64_t media_instance)
{
    return "content " + std::to_string(media_instance);
}

void note_frame_media(const SceneFrame& frame, std::set<std::string>& urls)
{
    const auto note = [&](const std::string& url) {
        if (url.starts_with("/static/")) {
            urls.insert(url);
        }
    };
    note(frame.background.image);
    note(frame.background.video);
    if (frame.image) {
        note(frame.image->url);
    }
    if (frame.video) {
        note(frame.video->url);
    }
    for (const auto& object : frame.objects) {
        if (!object.object) {
            continue;
        }
        if (const auto url = canvas::object_media_url(*object.object)) {
            note(*url);
        }
        if (const auto url = canvas::fill_picture_url(object.object->fill)) {
            note(*url);
        }
    }
    for (const auto& below : frame.stacked_below) {
        note_frame_media(*below, urls);
    }
}

}  // namespace seventhslide::scene
