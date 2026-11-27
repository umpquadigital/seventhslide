#include "audio_decoder.hpp"

#include "seventhslide/audio/audio_format.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>

namespace seventhslide::audio {
namespace {

/// FFmpeg reports to stderr; only what is really wrong, as mpv is set up.
void quiet_ffmpeg()
{
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_level(AV_LOG_ERROR); });
}

/// The most silence put before a sound track that starts late (a file that
/// says its sound starts minutes in is more likely wrong).
constexpr long k_max_lead_frames = 10L * k_sample_rate;

double seconds(AVRational r)
{
    return r.den != 0 ? av_q2d(r) : 0.0;
}

} // namespace

AudioDecoder::AudioDecoder() = default;

AudioDecoder::~AudioDecoder()
{
    close();
}

void AudioDecoder::close()
{
    swr_free(&resampler_);
    avcodec_free_context(&codec_);
    avformat_close_input(&format_);
    av_packet_free(&packet_);
    av_frame_free(&frame_);
    stream_ = -1;
    draining_ = false;
    in_format_ = -1;
    in_rate_ = 0;
    in_channels_ = 0;
}

bool AudioDecoder::open(const std::string& path)
{
    quiet_ffmpeg();
    close();
    path_ = path;
    if (avformat_open_input(&format_, path.c_str(), nullptr, nullptr) < 0) {
        return false;
    }
    if (avformat_find_stream_info(format_, nullptr) < 0) {
        close();
        return false;
    }
    const AVCodec* decoder = nullptr;
    stream_ = av_find_best_stream(format_, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (stream_ < 0 || decoder == nullptr) {
        close();
        return false;
    }
    // Only the sound is read; a video's picture is skipped in the demuxer.
    for (unsigned i = 0; i < format_->nb_streams; ++i) {
        if (static_cast<int>(i) != stream_) {
            format_->streams[i]->discard = AVDISCARD_ALL;
        }
    }
    const AVStream* st = format_->streams[stream_];
    codec_ = avcodec_alloc_context3(decoder);
    if (codec_ == nullptr || avcodec_parameters_to_context(codec_, st->codecpar) < 0) {
        close();
        return false;
    }
    codec_->pkt_timebase = st->time_base;
    if (avcodec_open2(codec_, decoder, nullptr) < 0) {
        close();
        return false;
    }
    packet_ = av_packet_alloc();
    frame_ = av_frame_alloc();
    if (packet_ == nullptr || frame_ == nullptr) {
        close();
        return false;
    }
    time_base_ = seconds(st->time_base);
    // Time counts from the start of the file, as mpv counts a video's: a
    // sound track that starts after the picture begins that far in.
    if (format_->start_time != AV_NOPTS_VALUE) {
        start_sec_ = static_cast<double>(format_->start_time) / AV_TIME_BASE;
    } else {
        start_sec_ = st->start_time != AV_NOPTS_VALUE ? st->start_time * time_base_ : 0.0;
    }
    if (format_->duration != AV_NOPTS_VALUE && format_->duration > 0) {
        duration_sec_ = static_cast<double>(format_->duration) / AV_TIME_BASE;
    } else if (st->duration != AV_NOPTS_VALUE && st->duration > 0) {
        duration_sec_ = st->duration * time_base_;
    } else {
        duration_sec_ = 0.0;
    }
    placing_ = true;
    place_at_sec_ = 0.0;
    return true;
}

bool AudioDecoder::ensure_resampler(const AVFrame* frame)
{
    const int channels = frame->ch_layout.nb_channels;
    if (resampler_ != nullptr && frame->format == in_format_ && frame->sample_rate == in_rate_
        && channels == in_channels_) {
        return true;
    }
    swr_free(&resampler_);
    if (channels <= 0 || frame->sample_rate <= 0) {
        return false;
    }
    AVChannelLayout in{};
    if (frame->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        av_channel_layout_default(&in, channels);
    } else if (av_channel_layout_copy(&in, &frame->ch_layout) < 0) {
        return false;
    }
    // Mono is resampled as mono and doubled (append), so it plays at full
    // level on both sides rather than 3 dB down, as a downmix would put it.
    mono_ = channels == 1;
    AVChannelLayout out{};
    av_channel_layout_default(&out, mono_ ? 1 : k_channels);
    const int set = swr_alloc_set_opts2(&resampler_, &out, AV_SAMPLE_FMT_FLT, k_sample_rate, &in,
                                        static_cast<AVSampleFormat>(frame->format),
                                        frame->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&in);
    av_channel_layout_uninit(&out);
    if (set < 0 || swr_init(resampler_) < 0) {
        swr_free(&resampler_);
        return false;
    }
    in_format_ = frame->format;
    in_rate_ = frame->sample_rate;
    in_channels_ = channels;
    return true;
}

void AudioDecoder::append(const float* frames, int count, std::vector<float>& out)
{
    if (count <= 0) {
        return;
    }
    if (!mono_) {
        out.insert(out.end(), frames, frames + static_cast<std::size_t>(count) * k_channels);
        return;
    }
    out.reserve(out.size() + static_cast<std::size_t>(count) * k_channels);
    for (int i = 0; i < count; ++i) {
        out.push_back(frames[i]);
        out.push_back(frames[i]);
    }
}

bool AudioDecoder::convert(const AVFrame* frame, std::vector<float>& out)
{
    if (!ensure_resampler(frame)) {
        return false;
    }
    const int room = swr_get_out_samples(resampler_, frame->nb_samples);
    if (room <= 0) {
        return true;
    }
    const int width = mono_ ? 1 : k_channels;
    converted_.resize(static_cast<std::size_t>(room) * width);
    auto* dst = reinterpret_cast<std::uint8_t*>(converted_.data());
    const int got = swr_convert(resampler_, &dst, room,
                                const_cast<const std::uint8_t**>(frame->extended_data),
                                frame->nb_samples);
    if (got < 0) {
        return false;
    }
    int skip = 0;
    if (placing_) {
        const std::int64_t pts = frame->best_effort_timestamp;
        if (pts != AV_NOPTS_VALUE) {
            const double at = pts * time_base_ - start_sec_;
            const auto early = std::lround((place_at_sec_ - at) * k_sample_rate);
            if (early >= 0) {
                // Before the place asked for (a seek lands on a packet
                // boundary): dropped.
                skip = static_cast<int>(std::min<long>(early, got));
            } else {
                // The sound starts later than asked: silence until it does.
                const auto late = std::min<long>(-early, k_max_lead_frames);
                out.insert(out.end(), static_cast<std::size_t>(late) * k_channels, 0.0f);
            }
        }
        // A frame that reaches the place ends the placing; so does one
        // without a timestamp, which cannot be placed.
        if (skip < got) {
            placing_ = false;
        }
    }
    append(converted_.data() + static_cast<std::size_t>(skip) * width, got - skip, out);
    return true;
}

void AudioDecoder::flush_resampler(std::vector<float>& out)
{
    if (resampler_ == nullptr) {
        return;
    }
    const int room = swr_get_out_samples(resampler_, 0);
    if (room <= 0) {
        return;
    }
    const int width = mono_ ? 1 : k_channels;
    converted_.resize(static_cast<std::size_t>(room) * width);
    auto* dst = reinterpret_cast<std::uint8_t*>(converted_.data());
    const int got = swr_convert(resampler_, &dst, room, nullptr, 0);
    if (got > 0 && !placing_) {
        append(converted_.data(), got, out);
    }
}

AudioDecoder::Status AudioDecoder::decode(std::vector<float>& out)
{
    if (codec_ == nullptr) {
        return Status::Failed;
    }
    const std::size_t had = out.size();
    for (;;) {
        const int received = avcodec_receive_frame(codec_, frame_);
        if (received == 0) {
            const bool ok = convert(frame_, out);
            av_frame_unref(frame_);
            if (!ok) {
                return Status::Failed;
            }
            if (out.size() > had) {
                return Status::Data;
            }
            continue;
        }
        if (received == AVERROR_EOF) {
            flush_resampler(out);
            return out.size() > had ? Status::Data : Status::End;
        }
        if (received != AVERROR(EAGAIN)) {
            return Status::Failed;
        }
        if (draining_) {
            return Status::End;
        }
        if (av_read_frame(format_, packet_) < 0) {
            // The end, or a read error: either way, what is decoded plays out.
            draining_ = true;
            avcodec_send_packet(codec_, nullptr);
            continue;
        }
        if (packet_->stream_index == stream_) {
            const int sent = avcodec_send_packet(codec_, packet_);
            // A damaged packet is skipped; the next may decode.
            if (sent < 0 && sent != AVERROR_INVALIDDATA && sent != AVERROR(EAGAIN)) {
                av_packet_unref(packet_);
                return Status::Failed;
            }
        }
        av_packet_unref(packet_);
    }
}

bool AudioDecoder::seek(double position_sec)
{
    if (format_ == nullptr || codec_ == nullptr) {
        return false;
    }
    position_sec = std::max(0.0, position_sec);
    bool ok = false;
    if (time_base_ > 0.0) {
        const auto ts = static_cast<std::int64_t>(std::llround((position_sec + start_sec_) / time_base_));
        ok = avformat_seek_file(format_, stream_, INT64_MIN, ts, ts, 0) >= 0;
    }
    if (!ok) {
        // A file that cannot seek starts again and decodes up to the target.
        const std::string path = path_;
        if (!open(path)) {
            return false;
        }
    } else {
        avcodec_flush_buffers(codec_);
        swr_free(&resampler_);
        in_format_ = -1;
        draining_ = false;
    }
    placing_ = true;
    place_at_sec_ = position_sec;
    return true;
}

} // namespace seventhslide::audio
