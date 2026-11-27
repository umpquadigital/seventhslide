#include "video_encoder.hpp"

#include "ffmpeg_util.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <algorithm>

namespace seventhslide::stream {
namespace {

[[nodiscard]] bool starts_with(const std::string& s, const char* prefix)
{
    return s.rfind(prefix, 0) == 0;
}

[[nodiscard]] bool ends_with(const std::string& s, const char* suffix)
{
    const std::string tail(suffix);
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

/// An option the encoder may not have (an older FFmpeg): not an error.
void set_option(AVCodecContext* ctx, const char* key, const char* value)
{
    (void)av_opt_set(ctx->priv_data, key, value, 0);
}

/// Each family's own way of saying "stream it, now".
void tune(AVCodecContext* ctx, const std::string& name, bool low_delay)
{
    if (name == "libx264") {
        set_option(ctx, "preset", "veryfast");
        set_option(ctx, "tune", "zerolatency");
    } else if (name == "libx265") {
        set_option(ctx, "preset", "ultrafast");
        set_option(ctx, "tune", "zerolatency");
        set_option(ctx, "x265-params", "log-level=error");
    } else if (ends_with(name, "_nvenc")) {
        set_option(ctx, "preset", "p3");
        set_option(ctx, "tune", low_delay ? "ull" : "ll");
        set_option(ctx, "rc", "cbr");
        set_option(ctx, "zerolatency", "1");
        set_option(ctx, "delay", "0");
    } else if (ends_with(name, "_qsv")) {
        set_option(ctx, "preset", "veryfast");
        set_option(ctx, "look_ahead", "0");
        set_option(ctx, "async_depth", low_delay ? "1" : "4");
    } else if (ends_with(name, "_vaapi")) {
        set_option(ctx, "rc_mode", "CBR");
        set_option(ctx, "async_depth", low_delay ? "1" : "4");
    } else if (ends_with(name, "_videotoolbox")) {
        // No constant_bit_rate: with AV_CODEC_FLAG_LOW_DELAY, FFmpeg asks
        // VideoToolbox for its low-latency rate control, which refuses
        // constant bitrate ("-constant_bit_rate true is not supported"), and
        // the encoder fails to open. Then SRT fell back to libx264 on every
        // Mac. The bitrate and rc_max_rate/rc_buffer_size (VideoToolbox's
        // data-rate limit) hold it close to the rate instead.
        set_option(ctx, "realtime", "1");
    }
}

/// What a packet's parameter sets say of its pictures: the size they show
/// at, and the size they are coded at before the crop. Zero where it does
/// not say (no parameter sets in it).
struct CodedSize {
    int width = 0;
    int height = 0;
    int coded_width = 0;
    int coded_height = 0;
};

[[nodiscard]] CodedSize coded_size(AVCodecID codec, const AVPacket* packet)
{
    CodedSize out;
    AVCodecParserContext* parser = av_parser_init(codec);
    // A context of its own: the parser writes what it finds into it.
    AVCodecContext* scratch = avcodec_alloc_context3(nullptr);
    if (parser != nullptr && scratch != nullptr) {
        scratch->codec_id = codec;
        parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
        std::uint8_t* data = nullptr;
        int size = 0;
        av_parser_parse2(parser, scratch, &data, &size, packet->data, packet->size,
                         AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        out = {parser->width, parser->height, parser->coded_width, parser->coded_height};
    }
    avcodec_free_context(&scratch);
    av_parser_close(parser);
    return out;
}

}  // namespace

VideoEncoder::~VideoEncoder()
{
    close();
}

void VideoEncoder::close()
{
    avcodec_free_context(&ctx_);
    av_buffer_unref(&hw_device_);
    av_frame_free(&converted_);
    av_packet_free(&packet_);
    sws_freeContext(sws_);
    sws_ = nullptr;
    sws_width_ = 0;
    sws_height_ = 0;
    vaapi_ = false;
    size_checked_ = false;
    av_bsf_free(&crop_);
    av_packet_free(&cropped_);
}

bool VideoEncoder::open(const std::string& name, const Config& config, std::string& error)
{
    close();
    quiet_ffmpeg();
    const AVCodec* codec = avcodec_find_encoder_by_name(name.c_str());
    if (codec == nullptr) {
        error = "This build of FFmpeg has no " + name + " encoder.";
        return false;
    }
    vaapi_ = ends_with(name, "_vaapi");
    const bool software = starts_with(name, "lib");
    const AVPixelFormat memory_format = software ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;

    ctx_ = avcodec_alloc_context3(codec);
    packet_ = av_packet_alloc();
    converted_ = av_frame_alloc();
    if (ctx_ == nullptr || packet_ == nullptr || converted_ == nullptr) {
        error = "Out of memory opening the video encoder.";
        close();
        return false;
    }
    ctx_->width = config.width;
    ctx_->height = config.height;
    ctx_->time_base = AVRational{1, config.frame_rate};
    ctx_->framerate = AVRational{config.frame_rate, 1};
    ctx_->gop_size = std::max(1, config.keyframe_interval);
    ctx_->max_b_frames = 0;
    ctx_->bit_rate = std::int64_t{config.bitrate_kbps} * 1000;
    ctx_->rc_max_rate = ctx_->bit_rate;
    // Half a second of buffer streaming locally, two over the internet:
    // a smaller one holds each picture closer to the bitrate, so less waits.
    ctx_->rc_buffer_size = static_cast<int>(config.low_delay ? ctx_->bit_rate / 2
                                                             : ctx_->bit_rate * 2);
    ctx_->sample_aspect_ratio = AVRational{1, 1};
    // HD video as receivers expect it: BT.709, limited range.
    ctx_->color_range = AVCOL_RANGE_MPEG;
    ctx_->colorspace = AVCOL_SPC_BT709;
    ctx_->color_primaries = AVCOL_PRI_BT709;
    ctx_->color_trc = AVCOL_TRC_BT709;
    if (config.low_delay) {
        ctx_->flags |= AV_CODEC_FLAG_LOW_DELAY;
    }
    tune(ctx_, name, config.low_delay);

    if (vaapi_) {
        int ret = av_hwdevice_ctx_create(&hw_device_, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
        AVBufferRef* frames = ret >= 0 ? av_hwframe_ctx_alloc(hw_device_) : nullptr;
        if (frames == nullptr) {
            error = "No VAAPI device to encode on: " + av_error_text(ret);
            close();
            return false;
        }
        auto* fc = reinterpret_cast<AVHWFramesContext*>(frames->data);
        fc->format = AV_PIX_FMT_VAAPI;
        fc->sw_format = AV_PIX_FMT_NV12;
        fc->width = config.width;
        fc->height = config.height;
        fc->initial_pool_size = 4;
        ret = av_hwframe_ctx_init(frames);
        if (ret < 0) {
            av_buffer_unref(&frames);
            error = "Could not set up VAAPI pictures: " + av_error_text(ret);
            close();
            return false;
        }
        ctx_->hw_frames_ctx = frames;  // the context's own reference now
        ctx_->pix_fmt = AV_PIX_FMT_VAAPI;
    } else {
        ctx_->pix_fmt = memory_format;
    }

    const int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        error = "Could not open the " + name + " encoder: " + av_error_text(ret);
        close();
        return false;
    }

    converted_->format = memory_format;
    converted_->width = config.width;
    converted_->height = config.height;
    if (av_frame_get_buffer(converted_, 0) < 0) {
        error = "Out of memory opening the video encoder.";
        close();
        return false;
    }
    return true;
}

bool VideoEncoder::encode(const std::uint8_t* bgrx, int stride, int width, int height,
                          std::int64_t pts, const PacketSink& out)
{
    if (ctx_ == nullptr || bgrx == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (sws_ == nullptr || width != sws_width_ || height != sws_height_) {
        sws_freeContext(sws_);
        sws_ = sws_getContext(width, height, AV_PIX_FMT_BGR0, ctx_->width, ctx_->height,
                              static_cast<AVPixelFormat>(converted_->format), SWS_BILINEAR,
                              nullptr, nullptr, nullptr);
        if (sws_ == nullptr) {
            return false;
        }
        // Full-range RGB in, limited-range BT.709 out.
        const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws_, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
        sws_width_ = width;
        sws_height_ = height;
    }
    if (av_frame_make_writable(converted_) < 0) {
        return false;
    }
    const std::uint8_t* src[] = {bgrx};
    const int src_stride[] = {stride};
    sws_scale(sws_, src, src_stride, 0, height, converted_->data, converted_->linesize);

    AVFrame* picture = converted_;
    AVFrame* uploaded = nullptr;
    if (vaapi_) {
        uploaded = av_frame_alloc();
        if (uploaded == nullptr || av_hwframe_get_buffer(ctx_->hw_frames_ctx, uploaded, 0) < 0
            || av_hwframe_transfer_data(uploaded, converted_, 0) < 0) {
            av_frame_free(&uploaded);
            return false;
        }
        picture = uploaded;
    }
    picture->pts = pts;
    picture->color_range = AVCOL_RANGE_MPEG;
    picture->colorspace = AVCOL_SPC_BT709;
    picture->color_primaries = AVCOL_PRI_BT709;
    picture->color_trc = AVCOL_TRC_BT709;
    const int ret = avcodec_send_frame(ctx_, picture);
    av_frame_free(&uploaded);
    return ret >= 0 && drain(out);
}

bool VideoEncoder::drain(const PacketSink& out)
{
    for (;;) {
        const int ret = avcodec_receive_packet(ctx_, packet_);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return true;
        }
        if (ret < 0) {
            return false;
        }
        if (!size_checked_) {
            size_checked_ = true;
            check_size(packet_);
        }
        if (crop_ == nullptr) {
            const bool keep = out(packet_);
            av_packet_unref(packet_);
            if (!keep) {
                return false;
            }
            continue;
        }
        // Through the crop: one packet in, one out.
        if (av_bsf_send_packet(crop_, packet_) < 0) {
            av_packet_unref(packet_);
            return false;
        }
        for (;;) {
            const int got = av_bsf_receive_packet(crop_, cropped_);
            if (got == AVERROR(EAGAIN)) {
                break;
            }
            if (got < 0) {
                return false;
            }
            const bool keep = out(cropped_);
            av_packet_unref(cropped_);
            if (!keep) {
                return false;
            }
        }
    }
}

void VideoEncoder::check_size(const AVPacket* first)
{
    const CodedSize size = coded_size(ctx_->codec_id, first);
    const bool as_asked = size.width == ctx_->width && size.height == ctx_->height;
    // Not told (no parameter sets), or coded smaller than asked: nothing a
    // crop can put right.
    if (as_asked || size.width == 0 || size.coded_width < ctx_->width
        || size.coded_height < ctx_->height) {
        return;
    }
    const char* name = ctx_->codec_id == AV_CODEC_ID_HEVC ? "hevc_metadata" : "h264_metadata";
    const AVBitStreamFilter* filter = av_bsf_get_by_name(name);
    cropped_ = av_packet_alloc();
    if (filter == nullptr || cropped_ == nullptr || av_bsf_alloc(filter, &crop_) < 0) {
        av_packet_free(&cropped_);
        return;  // sent as it is: padded, but a picture
    }
    avcodec_parameters_from_context(crop_->par_in, ctx_);
    crop_->time_base_in = ctx_->time_base;
    // Whatever the encoder wrote, the padding is all on the right and at
    // the bottom.
    av_opt_set_int(crop_->priv_data, "crop_left", 0, 0);
    av_opt_set_int(crop_->priv_data, "crop_top", 0, 0);
    av_opt_set_int(crop_->priv_data, "crop_right", size.coded_width - ctx_->width, 0);
    av_opt_set_int(crop_->priv_data, "crop_bottom", size.coded_height - ctx_->height, 0);
    if (av_bsf_init(crop_) < 0) {
        av_bsf_free(&crop_);
        av_packet_free(&cropped_);
    }
}

}  // namespace seventhslide::stream
