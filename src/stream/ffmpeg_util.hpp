#pragma once

// What the stream module's FFmpeg code shares. Private to the module.
// Seam: FFmpeg.

#include <string>

namespace seventhslide::stream {

/// FFmpeg says only what is really wrong (as the audio module has it).
void quiet_ffmpeg();

/// FFmpeg's words for error ``code``.
[[nodiscard]] std::string av_error_text(int code);

}  // namespace seventhslide::stream
