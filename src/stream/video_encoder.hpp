#pragma once

// One H.264 or H.265 encoder, by FFmpeg name, fed 8-bit BGRX pictures and
// set up to stream: constant bitrate, no B-frames, a keyframe at a fixed
// interval, as little delay inside the encoder as it allows. Hardware ones
// that take pictures in memory (NVENC, Quick Sync, VideoToolbox) are given
// NV12; VAAPI is given NV12 uploaded to the GPU; the software ones YUV420P.
//
// An encoder codes a picture padded out to its own block size and says in
// the stream how much of that to crop. Some hardware gets that wrong (AMD's
// VAAPI H.265 sends 1080p as 1088 lines, a green band under the picture), so
// the first packet's parameter sets are read back, and where the size they
// give is not the one asked for, the crop is written into the stream again
// (FFmpeg's h264_metadata / hevc_metadata).
// Private to the stream module.
// Seam: FFmpeg.

#include <cstdint>
#include <functional>
#include <string>

extern "C" {
struct AVBSFContext;
struct AVBufferRef;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
}

namespace seventhslide::stream {

class VideoEncoder {
public:
    struct Config {
        int width = 0;
        int height = 0;
        int frame_rate = 0;
        int bitrate_kbps = 0;
        /// Pictures from one keyframe to the next.
        int keyframe_interval = 0;
        /// Shave the encoder's own delay (the low delay preset); otherwise
        /// it may look a little ahead where that costs no B-frames.
        bool low_delay = true;
    };
    /// Called with each packet, in the encoder's time base; false stops.
    using PacketSink = std::function<bool(AVPacket*)>;

    VideoEncoder() = default;
    ~VideoEncoder();
    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    /// Open the encoder FFmpeg calls ``name``. False, with ``error`` set,
    /// when this build or this computer cannot.
    bool open(const std::string& name, const Config& config, std::string& error);
    void close();
    [[nodiscard]] bool is_open() const noexcept { return ctx_ != nullptr; }
    /// The open encoder (its parameters go into the stream); null when
    /// closed.
    [[nodiscard]] const AVCodecContext* context() const noexcept { return ctx_; }

    /// Encode one picture (8-bit B,G,R,X rows, top first; scaled when it is
    /// not the size opened at) as picture ``pts`` (in pictures since the
    /// start), and hand on what comes out. False when encoding failed or
    /// ``out`` said stop.
    bool encode(const std::uint8_t* bgrx, int stride, int width, int height, std::int64_t pts,
                const PacketSink& out);

private:
    bool drain(const PacketSink& out);
    /// Read the size the encoder's ``first`` packet codes pictures at, and
    /// set ``crop_`` up when it is not the size asked for.
    void check_size(const AVPacket* first);

    AVCodecContext* ctx_ = nullptr;
    AVBufferRef* hw_device_ = nullptr;
    /// The picture converted, in memory; for VAAPI then uploaded.
    AVFrame* converted_ = nullptr;
    AVPacket* packet_ = nullptr;
    SwsContext* sws_ = nullptr;
    /// The picture size ``sws_`` converts from.
    int sws_width_ = 0;
    int sws_height_ = 0;
    bool vaapi_ = false;
    bool size_checked_ = false;
    /// Writes the right crop into each packet, for an encoder that gets it
    /// wrong; null for the rest.
    AVBSFContext* crop_ = nullptr;
    AVPacket* cropped_ = nullptr;
};

}  // namespace seventhslide::stream
