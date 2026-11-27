#include "ffmpeg_live_input.hpp"

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/render/deinterlace.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#if defined(__APPLE__)
#include "capture_devices_mac.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <cstring>
#include <unistd.h>
#endif

namespace seventhslide::app {
namespace {

void register_devices()
{
    static std::once_flag once;
    std::call_once(once, [] { avdevice_register_all(); });
}

std::string av_error(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

struct FormatCloser {
    void operator()(AVFormatContext* c) const { avformat_close_input(&c); }
};
struct CodecCloser {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct FrameCloser {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketCloser {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct SwsCloser {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};
struct SwrCloser {
    void operator()(SwrContext* s) const { swr_free(&s); }
};

std::unique_ptr<AVCodecContext, CodecCloser> open_decoder(const AVStream* stream)
{
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        return nullptr;
    }
    std::unique_ptr<AVCodecContext, CodecCloser> ctx(avcodec_alloc_context3(codec));
    if (!ctx || avcodec_parameters_to_context(ctx.get(), stream->codecpar) < 0
        || avcodec_open2(ctx.get(), codec, nullptr) < 0) {
        return nullptr;
    }
    return ctx;
}

/// The picture is in fields (a capture card's 1080i, a broadcast stream).
[[nodiscard]] bool interlaced(const AVFrame& frame)
{
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 7, 100)
    return (frame.flags & AV_FRAME_FLAG_INTERLACED) != 0;
#else
    return frame.interlaced_frame != 0;
#endif
}

/// What a capture node says of itself.
struct NodeInfo {
    /// It captures pictures: a webcam's second V4L2 node carries only
    /// metadata, and does not.
    bool video = true;
    /// Where it is plugged in (V4L2's bus, ``usb-0000:00:14.0-2``): the
    /// same from one start to the next while it stays in that port. Empty
    /// where it cannot be asked.
    std::string port;
};

[[nodiscard]] [[maybe_unused]] NodeInfo node_info(const std::string& path)
{
#if defined(__linux__)
    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        return {};  // busy, or not ours to ask: let it be tried
    }
    v4l2_capability cap{};
    const bool asked = ::ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0;
    ::close(fd);
    if (!asked) {
        return {};
    }
    const auto caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) != 0 ? cap.device_caps
                                                                    : cap.capabilities;
    const auto* bus = reinterpret_cast<const char*>(cap.bus_info);
    return {(caps & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) != 0,
            std::string(bus, ::strnlen(bus, sizeof cap.bus_info))};
#else
    (void)path;
    return {};
#endif
}

} // namespace

const char* capture_input_format()
{
#if defined(_WIN32)
    return "dshow";
#elif defined(__APPLE__)
    return "avfoundation";
#else
    return "v4l2";
#endif
}

std::vector<CaptureDevice> tell_apart(std::vector<ListedDevice> listed)
{
    std::ranges::stable_sort(listed, [](const ListedDevice& a, const ListedDevice& b) {
        return a.device.name != b.device.name ? a.device.name < b.device.name : a.order < b.order;
    });
    std::vector<CaptureDevice> out;
    std::map<std::string, int> seen;
    for (auto& d : listed) {
        if (const int n = ++seen[d.device.name]; n > 1) {
            d.device.name += " (" + std::to_string(n) + ")";
        }
        out.push_back(std::move(d.device));
    }
    return out;
}

std::vector<CaptureDevice> capture_devices()
{
#if defined(__APPLE__)
    // FFmpeg's avfoundation lists nothing through avdevice (only into its
    // log): the cameras come from AVFoundation, and FFmpeg opens one by its
    // place in its list. Not by name: two of a model share one, and FFmpeg
    // reads a ':' in it as the end and a leading digit as a place.
    std::vector<ListedDevice> listed;
    for (auto& camera : mac_cameras()) {
        CaptureDevice device{camera.name, std::to_string(camera.index), {}};
        if (camera.frame_rate > 0.0) {
            char rate[32];
            std::snprintf(rate, sizeof rate, "%.3f", camera.frame_rate);
            device.options.emplace("framerate", rate);
        }
        listed.push_back({std::move(device), std::move(camera.unique_id)});
    }
    return tell_apart(std::move(listed));
#else
    register_devices();
    std::vector<CaptureDevice> out;
    const AVInputFormat* format = av_find_input_format(capture_input_format());
    if (format == nullptr) {
        return out;
    }
    AVDeviceInfoList* list = nullptr;
    if (avdevice_list_input_sources(format, nullptr, nullptr, &list) < 0 || list == nullptr) {
        avdevice_free_list_devices(&list);
        return out;
    }
    std::vector<ListedDevice> found;
    for (int i = 0; i < list->nb_devices; ++i) {
        const AVDeviceInfo* d = list->devices[i];
        if (d == nullptr || d->device_name == nullptr) {
            continue;
        }
        // Video devices only: a device that says what it carries and has no
        // video is a microphone.
        bool video = d->nb_media_types == 0;
        for (int m = 0; m < d->nb_media_types; ++m) {
            video = video || d->media_types[m] == AVMEDIA_TYPE_VIDEO;
        }
        if (!video) {
            continue;
        }
        auto node = node_info(d->device_name);
        if (!node.video) {
            continue;
        }
        std::string name = d->device_description != nullptr && d->device_description[0] != '\0'
                               ? d->device_description
                               : d->device_name;
        std::string url = d->device_name;
#if defined(_WIN32)
        url = "video=" + url;
#endif
        // Its port, else what FFmpeg opens it by (DirectShow's is the
        // device's own path).
        std::string order = node.port.empty() ? url : std::move(node.port);
        found.push_back({{std::move(name), std::move(url)}, std::move(order)});
    }
    avdevice_free_list_devices(&list);
    return tell_apart(std::move(found));
#endif
}

struct FfmpegLiveInput::State {
    State(Spec spec, FrameReady on_frame, audio::StreamSource* sound)
        : spec_(std::move(spec))
        , on_frame_(std::move(on_frame))
        , sink_(sound != nullptr ? sound : &own_sound_)
    {
    }

    void run();
    /// Open the input and read it until it ends, fails or is stopped.
    void read_once();
    void set_problem(std::string text);
    /// Wait ``ms`` or until stopped; false when stopped.
    bool wait(int ms);
    /// ``fn`` (which calls back to the owner: the frame callback, the sound
    /// sink) unless stopped. Under ``call_mu_``, which stopping takes too, so
    /// none runs once the owner is gone.
    template <class Fn>
    bool unless_stopped(Fn&& fn)
    {
        std::lock_guard lock(call_mu_);
        if (stop_) {
            return false;
        }
        fn();
        return true;
    }

    Spec spec_;
    FrameReady on_frame_;
    audio::StreamSource own_sound_;
    audio::StreamSource* sink_ = &own_sound_;
    mutable std::mutex mu_;
    QImage frame_;
    Stats stats_;
    std::string problem_;
    std::atomic<bool> stop_{false};
    std::mutex wait_mu_;
    std::condition_variable wake_;
    std::mutex call_mu_;
    /// Set as run() returns; ``done_wake_`` tells stopping.
    bool done_ = false;
    std::condition_variable done_wake_;
};

FfmpegLiveInput::FfmpegLiveInput(Spec spec, FrameReady on_frame, audio::StreamSource* sound)
    : state_(std::make_shared<State>(std::move(spec), std::move(on_frame), sound))
{
    register_devices();
    thread_ = std::thread([state = state_] {
        state->run();
        {
            std::lock_guard lock(state->wait_mu_);
            state->done_ = true;
        }
        state->done_wake_.notify_all();
    });
}

FfmpegLiveInput::~FfmpegLiveInput()
{
    {
        // Under the lock its wait looks at it with, so the wake-up cannot
        // come between the look and the wait; and under the callbacks' lock,
        // so none is running once this returns.
        std::scoped_lock lock(state_->wait_mu_, state_->call_mu_);
        state_->stop_ = true;
    }
    state_->wake_.notify_all();
    if (!thread_.joinable()) {
        return;
    }
    bool done = false;
    {
        std::unique_lock lock(state_->wait_mu_);
        done = state_->done_wake_.wait_for(lock, std::chrono::milliseconds(k_stop_wait_ms),
                                           [this] { return state_->done_; });
    }
    if (done) {
        thread_.join();
    } else {
        // Blocked inside FFmpeg (a device that sends nothing). It holds the
        // state, calls back to no one now it is stopped, and ends on its own
        // if the device ever answers.
        thread_.detach();
    }
}

audio::StreamSource& FfmpegLiveInput::sound() noexcept
{
    return *state_->sink_;
}

QImage FfmpegLiveInput::frame() const
{
    std::lock_guard lock(state_->mu_);
    return state_->frame_;
}

FfmpegLiveInput::Stats FfmpegLiveInput::stats() const
{
    std::lock_guard lock(state_->mu_);
    return state_->stats_;
}

std::string FfmpegLiveInput::problem() const
{
    std::lock_guard lock(state_->mu_);
    return state_->problem_;
}

void FfmpegLiveInput::State::set_problem(std::string text)
{
    std::lock_guard lock(mu_);
    problem_ = std::move(text);
}

bool FfmpegLiveInput::State::wait(int ms)
{
    std::unique_lock lock(wait_mu_);
    wake_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stop_.load(); });
    return !stop_;
}

void FfmpegLiveInput::State::run()
{
    while (!stop_) {
        read_once();
        if (!wait(k_retry_ms)) {
            return;
        }
    }
}

void FfmpegLiveInput::State::read_once()
{
#if defined(__APPLE__)
    // avfoundation waits for its first frame minding no interrupt, and a
    // camera not (yet) allowed sends none: opened, this input could never be
    // stopped, and stopping it would hang the window. Try again later instead.
    if (spec_.format == "avfoundation" && !mac_camera_allowed()) {
        set_problem("The camera is not allowed: allow SeventhSlide in System Settings > "
                    "Privacy & Security > Camera.");
        return;
    }
#endif
    AVFormatContext* raw = avformat_alloc_context();
    if (raw == nullptr) {
        set_problem("Out of memory");
        return;
    }
    // A read that blocks (a device gone quiet, a stream stalled) gives way
    // when this input is stopped.
    raw->interrupt_callback.callback = [](void* self) -> int {
        return static_cast<State*>(self)->stop_.load() ? 1 : 0;
    };
    raw->interrupt_callback.opaque = this;
    AVDictionary* options = nullptr;
    for (const auto& [key, value] : spec_.options) {
        av_dict_set(&options, key.c_str(), value.c_str(), 0);
    }
    const AVInputFormat* format =
        spec_.format.empty() ? nullptr : av_find_input_format(spec_.format.c_str());
    const int opened = avformat_open_input(&raw, spec_.url.c_str(), format, &options);
    av_dict_free(&options);
    if (opened < 0) {
        set_problem("Cannot open it: " + av_error(opened));
        return;  // avformat_open_input freed the context
    }
    std::unique_ptr<AVFormatContext, FormatCloser> input(raw);
    if (const int found = avformat_find_stream_info(input.get(), nullptr); found < 0) {
        set_problem("Cannot read it: " + av_error(found));
        return;
    }
    const int video_index = av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audio_index = av_find_best_stream(input.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (video_index < 0) {
        set_problem("It sends no picture.");
        return;
    }
    const AVStream* video_stream = input->streams[video_index];
    auto video = open_decoder(video_stream);
    if (!video) {
        set_problem("Its picture cannot be decoded.");
        return;
    }
    std::unique_ptr<AVCodecContext, CodecCloser> sound;
    if (audio_index >= 0) {
        sound = open_decoder(input->streams[audio_index]);
    }
    const AVRational rate = video_stream->avg_frame_rate.num > 0 ? video_stream->avg_frame_rate
                                                                 : video_stream->r_frame_rate;

    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    std::unique_ptr<AVFrame, FrameCloser> decoded(av_frame_alloc());
    std::unique_ptr<SwsContext, SwsCloser> scaler;
    std::unique_ptr<SwrContext, SwrCloser> resampler;
    std::vector<float> samples;
    std::vector<float> stereo;
    bool mono = false;
    if (!unless_stopped([this] { sink_->reset(); })) {
        return;
    }

    // Paced by the input's own clock: a packet stamped ahead of the wall
    // clock waits for it.
    using clock = std::chrono::steady_clock;
    bool started = false;
    clock::time_point wall_start;
    double stamp_start = 0.0;

    while (!stop_) {
        const int read = av_read_frame(input.get(), packet.get());
        if (read < 0) {
            set_problem(read == AVERROR_EOF ? std::string("It stopped sending.")
                                            : "It stopped: " + av_error(read));
            return;
        }
        const AVStream* stream = input->streams[packet->stream_index];
        if (packet->pts != AV_NOPTS_VALUE) {
            const double stamp = static_cast<double>(packet->pts) * av_q2d(stream->time_base);
            if (!started) {
                started = true;
                wall_start = clock::now();
                stamp_start = stamp;
            }
            const auto due = wall_start + std::chrono::duration_cast<clock::duration>(
                                              std::chrono::duration<double>(stamp - stamp_start));
            const auto ahead = std::chrono::duration_cast<std::chrono::milliseconds>(due - clock::now());
            // More than a few seconds ahead is a jump in the stamps, not a
            // reason to stop: start the clock again.
            if (ahead.count() > 5000) {
                wall_start = clock::now();
                stamp_start = stamp;
            } else if (ahead.count() > 0 && !wait(static_cast<int>(ahead.count()))) {
                av_packet_unref(packet.get());
                return;
            }
        }
        if (packet->stream_index == video_index) {
            if (avcodec_send_packet(video.get(), packet.get()) >= 0) {
                while (avcodec_receive_frame(video.get(), decoded.get()) >= 0) {
                    const int w = decoded->width;
                    const int h = decoded->height;
                    scaler.reset(sws_getCachedContext(
                        scaler.release(), w, h, static_cast<AVPixelFormat>(decoded->format), w, h,
                        AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr));
                    if (!scaler) {
                        continue;
                    }
                    QImage picture(w, h, QImage::Format_RGB32);
                    uint8_t* planes[4] = {picture.bits(), nullptr, nullptr, nullptr};
                    const int strides[4] = {static_cast<int>(picture.bytesPerLine()), 0, 0, 0};
                    sws_scale(scaler.get(), decoded->data, decoded->linesize, 0, h, planes, strides);
                    if (interlaced(*decoded)) {
                        render::deinterlace(picture);
                    }
                    {
                        std::lock_guard lock(mu_);
                        frame_ = std::move(picture);
                        stats_.size = QSize(w, h);
                        stats_.frame_rate = rate.den > 0 ? av_q2d(rate) : 0.0;
                        ++stats_.frames;
                        problem_.clear();
                    }
                    if (on_frame_ && !unless_stopped(on_frame_)) {
                        av_packet_unref(packet.get());
                        return;
                    }
                }
            }
        } else if (sound && packet->stream_index == audio_index) {
            if (avcodec_send_packet(sound.get(), packet.get()) >= 0) {
                while (avcodec_receive_frame(sound.get(), decoded.get()) >= 0) {
                    if (!resampler) {
                        // A mono source is resampled as mono and heard on
                        // both sides at its level, as NDI's and a web page's
                        // are; anything else is mixed to stereo.
                        mono = decoded->ch_layout.nb_channels == 1;
                        AVChannelLayout layout = mono ? AVChannelLayout(AV_CHANNEL_LAYOUT_MONO)
                                                      : AVChannelLayout(AV_CHANNEL_LAYOUT_STEREO);
                        SwrContext* made = nullptr;
                        if (swr_alloc_set_opts2(&made, &layout, AV_SAMPLE_FMT_FLT,
                                                audio::k_sample_rate, &decoded->ch_layout,
                                                static_cast<AVSampleFormat>(decoded->format),
                                                decoded->sample_rate, 0, nullptr)
                                < 0
                            || swr_init(made) < 0) {
                            swr_free(&made);
                            sound.reset();  // heard no more; the picture goes on
                            break;
                        }
                        resampler.reset(made);
                    }
                    const int most = swr_get_out_samples(resampler.get(), decoded->nb_samples);
                    const std::size_t width = mono ? 1 : audio::k_channels;
                    samples.resize(static_cast<std::size_t>(std::max(most, 0)) * width);
                    auto* out = reinterpret_cast<uint8_t*>(samples.data());
                    const int got = swr_convert(resampler.get(), &out, most,
                                                const_cast<const uint8_t**>(decoded->extended_data),
                                                decoded->nb_samples);
                    if (got <= 0) {
                        continue;
                    }
                    const auto frames = static_cast<std::size_t>(got);
                    if (mono) {
                        stereo.resize(frames * audio::k_channels);
                        for (std::size_t i = 0; i < frames; ++i) {
                            stereo[i * audio::k_channels] = samples[i];
                            stereo[i * audio::k_channels + 1] = samples[i];
                        }
                        (void)unless_stopped(
                            [&] { sink_->push(stereo.data(), frames, audio::k_sample_rate); });
                    } else {
                        (void)unless_stopped(
                            [&] { sink_->push(samples.data(), frames, audio::k_sample_rate); });
                    }
                }
            }
        }
        av_packet_unref(packet.get());
    }
}

} // namespace seventhslide::app
