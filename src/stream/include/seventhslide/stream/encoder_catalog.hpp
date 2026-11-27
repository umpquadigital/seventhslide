#pragma once

// What this computer can stream with: whether FFmpeg was built with SRT, and
// which H.264 / H.265 encoders actually open here, hardware first (a GPU's
// encoder leaves the processor for rendering, and adds less delay). Which
// encoders a build has says little - NVENC is compiled into most builds and
// opens only beside an NVIDIA card - so each is tried once, and remembered.
// Seam: plain C++20 (FFmpeg behind it). Any thread.

#include "seventhslide/stream/stream_settings.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace seventhslide::stream {

struct EncoderInfo {
    /// FFmpeg's name for it: "hevc_vaapi", "libx264", ...
    std::string name;
    VideoCodec codec = VideoCodec::H265;
    /// On a GPU or a media engine rather than the processor.
    bool hardware = false;

    [[nodiscard]] bool operator==(const EncoderInfo&) const = default;
};

/// This build of FFmpeg can send SRT (it was built with libsrt).
[[nodiscard]] bool srt_supported();

/// The encoders of ``codec`` FFmpeg might have, in the order they are
/// preferred: hardware, then software.
[[nodiscard]] std::vector<std::string> encoder_candidates(VideoCodec codec);

/// The encoders of ``codec`` that open on this computer, best first. The
/// first call for a codec tries each candidate, which can take a second or
/// more where hardware drivers start up; later calls return what it found.
/// A caller on the window's thread asks ``known_encoders`` instead, and lets
/// ``probe_encoders`` find them on a worker.
[[nodiscard]] std::vector<EncoderInfo> usable_encoders(VideoCodec codec);

/// What ``usable_encoders`` found for ``codec``, or nullopt while it has not
/// been asked yet. Never waits for a probe.
[[nodiscard]] std::optional<std::vector<EncoderInfo>> known_encoders(VideoCodec codec);

/// Find every codec's usable encoders (``usable_encoders``). Slow the first
/// time: run it off the window's thread.
void probe_encoders();

/// Tests: forget what was found, and try each candidate with ``trial`` from
/// now on rather than by opening it (by opening it again when empty).
void set_encoder_trial_for_tests(std::function<bool(const std::string& name)> trial);

}  // namespace seventhslide::stream
