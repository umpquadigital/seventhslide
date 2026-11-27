#include "video_frame.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/log.h>
#include <libswscale/swscale.h>
}

#include <QTransform>

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace seventhslide::app {
namespace {

/// Past this many decoded frames the newest one will do: a stream with keyframes
/// minutes apart is not worth decoding all the way to the moment asked for.
constexpr int k_max_frames = 240;

/// The latest the frame is taken from, in seconds.
constexpr double k_max_at_s = 10.0;

void quiet_ffmpeg()
{
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_level(AV_LOG_ERROR); });
}

struct FormatClose {
    void operator()(AVFormatContext* f) const { avformat_close_input(&f); }
};
struct CodecFree {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct FrameFree {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketFree {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct ScaleFree {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};

using FramePtr = std::unique_ptr<AVFrame, FrameFree>;

/// Degrees clockwise the picture is turned to stand upright: 0, 90, 180 or 270.
int upright_turn(const AVStream& stream)
{
    const std::uint8_t* matrix = nullptr;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 30, 100)
    if (const auto* sd = av_packet_side_data_get(stream.codecpar->coded_side_data,
                                                 stream.codecpar->nb_coded_side_data,
                                                 AV_PKT_DATA_DISPLAYMATRIX)) {
        matrix = sd->data;
    }
#else
    matrix = av_stream_get_side_data(&stream, AV_PKT_DATA_DISPLAYMATRIX, nullptr);
#endif
    if (!matrix) {
        return 0;
    }
    const double angle = -av_display_rotation_get(reinterpret_cast<const std::int32_t*>(matrix));
    if (std::isnan(angle)) {
        return 0;
    }
    const int quarter = static_cast<int>(std::lround(angle / 90.0));
    return ((quarter % 4) + 4) % 4 * 90;
}

/// ``frame`` as an RGB image whose longest edge is at most ``max_edge``, at its
/// display shape (a non-square pixel is widened or narrowed).
QImage to_image(const AVFrame& frame, AVRational sample_aspect, int max_edge)
{
    if (frame.width <= 0 || frame.height <= 0) {
        return {};
    }
    double w = frame.width;
    const double h = frame.height;
    if (sample_aspect.num > 0 && sample_aspect.den > 0) {
        w *= av_q2d(sample_aspect);
    }
    const double scale = std::min(1.0, max_edge / std::max(w, h));
    const int out_w = std::max(1, static_cast<int>(std::lround(w * scale)));
    const int out_h = std::max(1, static_cast<int>(std::lround(h * scale)));

    std::unique_ptr<SwsContext, ScaleFree> sws(sws_getContext(
        frame.width, frame.height, static_cast<AVPixelFormat>(frame.format), out_w, out_h,
        AV_PIX_FMT_RGB24, SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!sws) {
        return {};
    }
    QImage image(out_w, out_h, QImage::Format_RGB888);
    if (image.isNull()) {
        return {};
    }
    std::uint8_t* const dst[4] = {image.bits(), nullptr, nullptr, nullptr};
    const int dst_stride[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};
    if (sws_scale(sws.get(), frame.data, frame.linesize, 0, frame.height, dst, dst_stride)
        != out_h) {
        return {};
    }
    return image;
}

} // namespace

QImage grab_video_frame(const std::filesystem::path& path, int max_edge)
{
    if (path.empty() || max_edge <= 0) {
        return {};
    }
    quiet_ffmpeg();

    AVFormatContext* opened = nullptr;
    if (avformat_open_input(&opened, path.string().c_str(), nullptr, nullptr) < 0) {
        return {};
    }
    const std::unique_ptr<AVFormatContext, FormatClose> format(opened);
    if (avformat_find_stream_info(format.get(), nullptr) < 0) {
        return {};
    }
    const AVCodec* codec = nullptr;
    const int index = av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (index < 0 || !codec) {
        return {};
    }
    AVStream* const stream = format->streams[index];

    const std::unique_ptr<AVCodecContext, CodecFree> decoder(avcodec_alloc_context3(codec));
    if (!decoder || avcodec_parameters_to_context(decoder.get(), stream->codecpar) < 0) {
        return {};
    }
    // Slices, not frames: frame threads hold back the first picture until
    // each has one.
    decoder->thread_count = 0;
    decoder->thread_type = FF_THREAD_SLICE;
    if (avcodec_open2(decoder.get(), codec, nullptr) < 0) {
        return {};
    }

    const bool cover = (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
    std::int64_t target = AV_NOPTS_VALUE;
    if (!cover && format->duration != AV_NOPTS_VALUE && format->duration > 0) {
        const double at = std::min(format->duration / double(AV_TIME_BASE) / 3.0, k_max_at_s);
        target = av_rescale_q(static_cast<std::int64_t>(at * AV_TIME_BASE), AV_TIME_BASE_Q,
                              stream->time_base);
        if (stream->start_time != AV_NOPTS_VALUE) {
            target += stream->start_time;
        }
        // A file that cannot seek is read from the start instead.
        if (av_seek_frame(format.get(), index, target, AVSEEK_FLAG_BACKWARD) >= 0) {
            avcodec_flush_buffers(decoder.get());
        }
    }

    const std::unique_ptr<AVPacket, PacketFree> packet(av_packet_alloc());
    const FramePtr decoded(av_frame_alloc());
    FramePtr kept;
    if (!packet || !decoded) {
        return {};
    }
    int frames = 0;
    bool done = false;
    // Every picture the decoder has ready; ``done`` once one is at the
    // moment asked for (or enough have been decoded).
    const auto drain = [&] {
        while (!done && avcodec_receive_frame(decoder.get(), decoded.get()) >= 0) {
            kept.reset(av_frame_clone(decoded.get()));
            av_frame_unref(decoded.get());
            const std::int64_t at = kept ? kept->best_effort_timestamp : AV_NOPTS_VALUE;
            done = cover || target == AV_NOPTS_VALUE || ++frames >= k_max_frames
                   || (at != AV_NOPTS_VALUE && at >= target);
        }
    };
    if (cover) {
        // A picture stored with the file (cover art) is its one frame.
        if (avcodec_send_packet(decoder.get(), &stream->attached_pic) >= 0) {
            drain();
        }
    }
    while (!cover && !done && av_read_frame(format.get(), packet.get()) >= 0) {
        if (packet->stream_index == index && avcodec_send_packet(decoder.get(), packet.get()) >= 0) {
            drain();
        }
        av_packet_unref(packet.get());
    }
    if (!done) {
        // The end of the file: what the decoder still holds.
        if (avcodec_send_packet(decoder.get(), nullptr) >= 0) {
            drain();
        }
    }
    if (!kept) {
        return {};
    }

    const AVRational sar = av_guess_sample_aspect_ratio(format.get(), stream, kept.get());
    QImage image = to_image(*kept, sar, max_edge);
    if (const int turn = upright_turn(*stream); turn != 0 && !image.isNull()) {
        image = image.transformed(QTransform().rotate(turn));
    }
    return image;
}

} // namespace seventhslide::app
