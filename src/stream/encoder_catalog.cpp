#include "seventhslide/stream/encoder_catalog.hpp"

#include "ffmpeg_util.hpp"
#include "video_encoder.hpp"

extern "C" {
#include <libavformat/avio.h>
}

#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

namespace seventhslide::stream {

bool srt_supported()
{
    static const bool supported = [] {
        void* opaque = nullptr;
        // Output protocols: the ones FFmpeg can write to.
        while (const char* name = avio_enum_protocols(&opaque, 1)) {
            if (std::strcmp(name, "srt") == 0) {
                return true;
            }
        }
        return false;
    }();
    return supported;
}

std::vector<std::string> encoder_candidates(VideoCodec codec)
{
    if (codec == VideoCodec::H265) {
        return {"hevc_nvenc", "hevc_qsv", "hevc_vaapi", "hevc_videotoolbox", "libx265"};
    }
    return {"h264_nvenc", "h264_qsv", "h264_vaapi", "h264_videotoolbox", "libx264"};
}

namespace {

struct Catalog {
    /// What the probes found, by codec. Its lock is held only to read or
    /// store an answer, never across a probe, so ``known_encoders`` cannot
    /// wait on one.
    std::mutex found_mu;
    std::map<VideoCodec, std::vector<EncoderInfo>> found;
    /// One probe at a time: two callers asking at once open each encoder once.
    std::mutex probe_mu;
    /// How a candidate is tried, under ``probe_mu``: opened, unless a test
    /// says otherwise.
    std::function<bool(const std::string&)> trial;
};

/// Never destroyed: a probe on a worker may still be running as the
/// program exits.
Catalog& catalog()
{
    static auto* const c = new Catalog;
    return *c;
}

}  // namespace

std::optional<std::vector<EncoderInfo>> known_encoders(VideoCodec codec)
{
    auto& c = catalog();
    std::lock_guard lock(c.found_mu);
    if (const auto it = c.found.find(codec); it != c.found.end()) {
        return it->second;
    }
    return std::nullopt;
}

void probe_encoders()
{
    for (const auto codec : {VideoCodec::H265, VideoCodec::H264}) {
        (void)usable_encoders(codec);
    }
}

std::vector<EncoderInfo> usable_encoders(VideoCodec codec)
{
    if (auto known = known_encoders(codec)) {
        return std::move(*known);
    }
    auto& c = catalog();
    std::lock_guard probing(c.probe_mu);
    if (auto known = known_encoders(codec)) {
        return std::move(*known);  // found while this caller waited
    }
    quiet_ffmpeg();
    std::vector<EncoderInfo> usable;
    // Small, so a software encoder opens quickly too.
    VideoEncoder::Config probe;
    probe.width = 640;
    probe.height = 360;
    probe.frame_rate = k_frame_rate;
    probe.bitrate_kbps = 1000;
    probe.keyframe_interval = k_frame_rate;
    for (const std::string& name : encoder_candidates(codec)) {
        VideoEncoder encoder;
        std::string error;
        if (c.trial ? c.trial(name) : encoder.open(name, probe, error)) {
            usable.push_back({name, codec, name.rfind("lib", 0) != 0});
        }
    }
    std::lock_guard lock(c.found_mu);
    c.found.emplace(codec, usable);
    return usable;
}

void set_encoder_trial_for_tests(std::function<bool(const std::string& name)> trial)
{
    auto& c = catalog();
    std::lock_guard probing(c.probe_mu);
    c.trial = std::move(trial);
    std::lock_guard lock(c.found_mu);
    c.found.clear();
}

}  // namespace seventhslide::stream
