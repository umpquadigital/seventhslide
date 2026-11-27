#pragma once

// A test's own SRT receiver, through FFmpeg as OBS or a decoder would read a
// stream: it connects (or waits), reads what the stream carries, and counts
// the packets of each kind that come. And a port for a test's stream.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <QHostAddress>
#include <QUdpSocket>

#include <chrono>
#include <map>
#include <string>

namespace seventhslide::test {

/// A UDP port free on every address now (SRT is UDP), so tests running side
/// by side each stream on their own. 0 when none can be had.
[[nodiscard]] inline int free_udp_port()
{
    QUdpSocket socket;
    if (!socket.bind(QHostAddress::AnyIPv4, 0)) {
        return 0;
    }
    return socket.localPort();
}

class SrtTestReceiver {
public:
    struct Info {
        std::string video_codec;  // "h264", "hevc"
        int width = 0;
        int height = 0;
        std::string audio_codec;  // "aac"
        int sample_rate = 0;
        int channels = 0;
    };
    struct Counts {
        int video = 0;
        int audio = 0;
    };

    SrtTestReceiver() = default;
    ~SrtTestReceiver() { avformat_close_input(&ctx_); }
    SrtTestReceiver(const SrtTestReceiver&) = delete;
    SrtTestReceiver& operator=(const SrtTestReceiver&) = delete;

    /// Open ``url`` with SRT ``options`` ("mode", "passphrase", ...) and read
    /// what the stream carries; false when that does not happen within
    /// ``timeout``.
    bool open(const std::string& url, const std::map<std::string, std::string>& options,
              std::chrono::milliseconds timeout)
    {
        deadline_ = std::chrono::steady_clock::now() + timeout;
        ctx_ = avformat_alloc_context();
        ctx_->interrupt_callback = AVIOInterruptCB{&SrtTestReceiver::interrupted, this};
        AVDictionary* dict = nullptr;
        for (const auto& [key, value] : options) {
            av_dict_set(&dict, key.c_str(), value.c_str(), 0);
        }
        const int ret = avformat_open_input(&ctx_, url.c_str(), nullptr, &dict);
        av_dict_free(&dict);
        if (ret < 0) {
            return false;  // avformat_open_input freed the context
        }
        if (avformat_find_stream_info(ctx_, nullptr) < 0) {
            return false;
        }
        for (unsigned i = 0; i < ctx_->nb_streams; ++i) {
            const AVCodecParameters* p = ctx_->streams[i]->codecpar;
            if (p->codec_type == AVMEDIA_TYPE_VIDEO) {
                video_ = static_cast<int>(i);
                info_.video_codec = avcodec_get_name(p->codec_id);
                info_.width = p->width;
                info_.height = p->height;
            } else if (p->codec_type == AVMEDIA_TYPE_AUDIO) {
                audio_ = static_cast<int>(i);
                info_.audio_codec = avcodec_get_name(p->codec_id);
                info_.sample_rate = p->sample_rate;
                info_.channels = p->ch_layout.nb_channels;
            }
        }
        return true;
    }

    [[nodiscard]] const Info& info() const noexcept { return info_; }

    /// Packets read for ``duration``.
    Counts read(std::chrono::milliseconds duration)
    {
        Counts out;
        deadline_ = std::chrono::steady_clock::now() + duration;
        AVPacket* packet = av_packet_alloc();
        while (std::chrono::steady_clock::now() < deadline_ && av_read_frame(ctx_, packet) >= 0) {
            out.video += packet->stream_index == video_ ? 1 : 0;
            out.audio += packet->stream_index == audio_ ? 1 : 0;
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        return out;
    }

private:
    static int interrupted(void* opaque)
    {
        return std::chrono::steady_clock::now() > static_cast<SrtTestReceiver*>(opaque)->deadline_
                   ? 1
                   : 0;
    }

    AVFormatContext* ctx_ = nullptr;
    std::chrono::steady_clock::time_point deadline_;
    Info info_;
    int video_ = -1;
    int audio_ = -1;
};

}  // namespace seventhslide::test
