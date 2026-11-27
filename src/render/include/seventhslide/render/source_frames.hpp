#pragma once

// Pictures drawn outside the scene by the source that makes them - a video
// item's player, a web page's renderer, a live source's receiver, a video
// object's player - and handed to whatever paints the frames that show them,
// keyed by source (``SceneVideoMedia::source``, ``SceneWebMedia::source``,
// ``SceneLiveMedia::source``, ``scene::video_object_key``). Seam: Qt Gui.

#include "seventhslide/scene/scene_frame.hpp"

#include <QImage>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace seventhslide::render {

/// How far a timed source (a video) has played: what a countdown painted
/// with it counts down.
struct SourceClock {
    double position_sec = 0.0;
    double duration_sec = 0.0;

    [[nodiscard]] bool operator==(const SourceClock&) const noexcept = default;
};

/// A source's latest picture, and a video's clock as of it.
struct SourceFrame {
    QImage image;
    std::optional<SourceClock> clock;
};

/// The latest of each source, by its key.
using SourceFrames = std::map<std::string, SourceFrame, std::less<>>;

/// ``key``'s picture in ``frames``; null when there is none (or no frames).
[[nodiscard]] const QImage* source_image(const SourceFrames* frames, std::string_view key);
/// ``frame``'s own video's clock in ``frames``; null when it shows none, or
/// it has not played yet.
[[nodiscard]] const SourceClock* video_clock_of(const scene::SceneFrame& frame,
                                                const SourceFrames* frames);

/// The sources ``frame`` itself shows: its stack's layer alone.
[[nodiscard]] std::vector<std::string> layer_sources_of(const scene::SceneFrame& frame);
/// The sources ``frame`` and the stacks below it show.
[[nodiscard]] std::vector<std::string> frame_sources_of(const scene::SceneFrame& frame);

} // namespace seventhslide::render
