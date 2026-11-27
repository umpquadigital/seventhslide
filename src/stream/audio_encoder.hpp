#pragma once

// The stream's sound: FFmpeg's own AAC encoder, at 48 kHz stereo, fed the
// mixer's interleaved float frames in whatever counts they come and handing
// on a packet per 1024 frames. Private to the stream module.
// Seam: FFmpeg.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

extern "C" {
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
}

namespace seventhslide::stream {

class AudioEncoder {
public:
    /// Called with each packet, in the encoder's time base (one per sample);
    /// false stops.
    using PacketSink = std::function<bool(AVPacket*)>;

    /// What a stream's sound is sent at: plenty for programme sound.
    static constexpr int k_bitrate_kbps = 160;

    AudioEncoder() = default;
    ~AudioEncoder();
    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    bool open(std::string& error);
    void close();
    [[nodiscard]] const AVCodecContext* context() const noexcept { return ctx_; }

    /// ``count`` interleaved stereo frames, the next after the last given.
    /// False when encoding failed or ``out`` said stop.
    bool add(const float* frames, std::size_t count, const PacketSink& out);
    /// Frames encoded since opened.
    [[nodiscard]] std::int64_t frames_encoded() const noexcept { return next_pts_; }

private:
    AVCodecContext* ctx_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* packet_ = nullptr;
    /// Interleaved frames waiting to make up a whole encoder frame.
    std::vector<float> pending_;
    std::int64_t next_pts_ = 0;
};

}  // namespace seventhslide::stream
