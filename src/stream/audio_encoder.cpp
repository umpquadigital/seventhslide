#include "audio_encoder.hpp"

#include "ffmpeg_util.hpp"

#include <seventhslide/audio/audio_format.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}

namespace seventhslide::stream {

AudioEncoder::~AudioEncoder()
{
    close();
}

void AudioEncoder::close()
{
    avcodec_free_context(&ctx_);
    av_frame_free(&frame_);
    av_packet_free(&packet_);
    pending_.clear();
    next_pts_ = 0;
}

bool AudioEncoder::open(std::string& error)
{
    close();
    quiet_ffmpeg();
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    ctx_ = codec != nullptr ? avcodec_alloc_context3(codec) : nullptr;
    frame_ = av_frame_alloc();
    packet_ = av_packet_alloc();
    if (ctx_ == nullptr || frame_ == nullptr || packet_ == nullptr) {
        error = "This build of FFmpeg has no AAC encoder.";
        close();
        return false;
    }
    ctx_->sample_rate = audio::k_sample_rate;
    ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_default(&ctx_->ch_layout, audio::k_channels);
    ctx_->bit_rate = std::int64_t{k_bitrate_kbps} * 1000;
    ctx_->time_base = AVRational{1, audio::k_sample_rate};
    const int ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        error = "Could not open the AAC encoder: " + av_error_text(ret);
        close();
        return false;
    }
    frame_->format = ctx_->sample_fmt;
    frame_->sample_rate = ctx_->sample_rate;
    frame_->nb_samples = ctx_->frame_size;
    if (av_channel_layout_copy(&frame_->ch_layout, &ctx_->ch_layout) < 0
        || av_frame_get_buffer(frame_, 0) < 0) {
        error = "Out of memory opening the AAC encoder.";
        close();
        return false;
    }
    return true;
}

bool AudioEncoder::add(const float* frames, std::size_t count, const PacketSink& out)
{
    if (ctx_ == nullptr) {
        return false;
    }
    pending_.insert(pending_.end(), frames, frames + count * audio::k_channels);
    const auto per_frame = static_cast<std::size_t>(ctx_->frame_size);
    std::size_t used = 0;
    while (pending_.size() - used >= per_frame * audio::k_channels) {
        if (av_frame_make_writable(frame_) < 0) {
            return false;
        }
        const float* in = pending_.data() + used;
        for (int c = 0; c < audio::k_channels; ++c) {
            auto* plane = reinterpret_cast<float*>(frame_->data[c]);
            for (std::size_t i = 0; i < per_frame; ++i) {
                plane[i] = in[i * audio::k_channels + static_cast<std::size_t>(c)];
            }
        }
        used += per_frame * audio::k_channels;
        frame_->pts = next_pts_;
        next_pts_ += static_cast<std::int64_t>(per_frame);
        if (avcodec_send_frame(ctx_, frame_) < 0) {
            return false;
        }
        for (;;) {
            const int ret = avcodec_receive_packet(ctx_, packet_);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            }
            if (ret < 0) {
                return false;
            }
            const bool keep = out(packet_);
            av_packet_unref(packet_);
            if (!keep) {
                return false;
            }
        }
    }
    pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(used));
    return true;
}

}  // namespace seventhslide::stream
