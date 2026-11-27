#include <seventhslide/render/source_frames.hpp>

#include <algorithm>

namespace seventhslide::render {

namespace {

void add_layer_sources(const scene::SceneFrame& f, std::vector<std::string>& out)
{
    const auto add = [&out](const std::string& source) {
        if (!source.empty() && std::ranges::find(out, source) == out.end()) {
            out.push_back(source);
        }
    };
    if (f.mode == scene::SceneMode::Video && f.video) {
        add(f.video->source);
    }
    if (f.mode == scene::SceneMode::Web && f.web) {
        add(f.web->source);
    }
    if (f.mode == scene::SceneMode::Live && f.live) {
        add(f.live->source);
    }
    for (const auto& obj : f.objects) {
        if (obj.object && obj.object->type == canvas::ObjectType::Video) {
            if (const auto url = canvas::object_media_url(*obj.object)) {
                add(scene::video_object_key(*url));
            }
        }
    }
}

}  // namespace

std::vector<std::string> layer_sources_of(const scene::SceneFrame& frame)
{
    std::vector<std::string> out;
    add_layer_sources(frame, out);
    return out;
}

std::vector<std::string> frame_sources_of(const scene::SceneFrame& frame)
{
    std::vector<std::string> out;
    add_layer_sources(frame, out);
    for (const auto& below : frame.stacked_below) {
        add_layer_sources(*below, out);
    }
    return out;
}

const QImage* source_image(const SourceFrames* frames, std::string_view key)
{
    if (frames == nullptr || key.empty()) {
        return nullptr;
    }
    const auto it = frames->find(key);
    return it != frames->end() && !it->second.image.isNull() ? &it->second.image : nullptr;
}

const SourceClock* video_clock_of(const scene::SceneFrame& frame, const SourceFrames* frames)
{
    if (frames == nullptr || !frame.video || frame.video->source.empty()) {
        return nullptr;
    }
    const auto it = frames->find(frame.video->source);
    return it != frames->end() && it->second.clock ? &*it->second.clock : nullptr;
}

} // namespace seventhslide::render
