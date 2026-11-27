#pragma once

// One still picture of a video, for its thumbnail in the library. Decoded
// here with the FFmpeg libraries the Audio Mixer already uses, so it needs
// neither the ffmpeg program nor a player.

#include <QImage>

#include <filesystem>

namespace seventhslide::app {

/// A frame of the video at ``path``: a third of the way in but no more than
/// ten seconds (past a fade from black, not far into a long one), turned
/// upright as the video says, its longest edge at most ``max_edge``. Null
/// when the file has no picture or it cannot be decoded.
[[nodiscard]] QImage grab_video_frame(const std::filesystem::path& path, int max_edge);

} // namespace seventhslide::app
