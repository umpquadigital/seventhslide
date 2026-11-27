#include "seventhslide/stream/srt_streamer.hpp"

#include "audio_encoder.hpp"
#include "ffmpeg_util.hpp"
#include "video_encoder.hpp"

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/audio/drift_buffer.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace seventhslide::stream {
namespace {

using Clock = std::chrono::steady_clock;

/// How long after losing a receiver, or failing to reach one, it tries again.
constexpr auto k_retry_after = std::chrono::seconds(2);
/// How often a stream waiting for the encoders to be tried sees whether it
/// has been stopped.
constexpr auto k_probe_check = std::chrono::milliseconds(20);
/// How long a caller waits for the receiver to answer.
constexpr int k_connect_timeout_ms = 3000;
/// Seven MPEG-TS packets: what fits one SRT packet on an Ethernet link.
constexpr const char* k_srt_packet_size = "1316";
/// A sender this far behind (a stall, a suspended laptop) carries on from
/// now rather than racing to catch up.
constexpr auto k_resync_after = std::chrono::milliseconds(500);
/// The most sound taken at once, after such a stall.
constexpr std::int64_t k_max_audio_step = 2 * audio::k_sample_rate;
/// The sound's own clock: a block every 10 ms, as the mixer makes them.
/// The DriftBuffer keeps 40 ms between the mixer's clock and this one; read
/// a picture's worth (33 ms) at a time it would run dry whenever the
/// mixer's blocks came a little late.
constexpr auto k_sound_period = std::chrono::milliseconds(10);

/// What a connection's sound clock hands its sending loop.
struct SoundTaken {
    std::mutex mu;
    /// Interleaved stereo frames, oldest first.
    std::vector<float> frames;
};

/// What the operator is told when no encoder of ``codec`` opens here.
std::string no_encoder_text(VideoCodec codec)
{
    return codec == VideoCodec::H265 ? "No H.265 encoder works on this computer. Choose H.264."
                                     : "No H.264 encoder works on this computer.";
}

}  // namespace

std::string_view state_name(StreamState state)
{
    switch (state) {
    case StreamState::Stopped:
        return "stopped";
    case StreamState::Waiting:
        return "waiting";
    case StreamState::Connecting:
        return "connecting";
    case StreamState::Sending:
        return "sending";
    case StreamState::Retrying:
        return "retrying";
    case StreamState::Failed:
        return "failed";
    }
    return "stopped";
}

struct SrtStreamer::Impl {
    StreamSettings settings;
    int width = 0;
    int height = 0;
    std::vector<EncoderInfo> encoders;

    std::thread thread;
    std::atomic<bool> stopping{false};
    std::mutex wake_mu;
    std::condition_variable wake;

    /// The newest picture from the GUI thread, swapped out by the streaming
    /// thread when ``serial`` moves on.
    std::mutex picture_mu;
    std::vector<std::uint8_t> picture;
    int picture_width = 0;
    int picture_height = 0;
    int picture_stride = 0;
    std::uint64_t picture_serial = 0;

    audio::DriftBuffer sound;

    mutable std::mutex status_mu;
    StreamStatus status;
    StatusListener listener;

    std::atomic<std::int64_t> pictures_sent{0};
    std::atomic<std::int64_t> audio_frames_sent{0};
    /// It cannot stream at all: no more connections are tried.
    bool failed = false;

    static int interrupted(void* opaque)
    {
        return static_cast<Impl*>(opaque)->stopping.load() ? 1 : 0;
    }

    template <typename Change>
    void update(Change change)
    {
        bool moved = false;
        {
            std::lock_guard lock(status_mu);
            StreamStatus next = status;
            change(next);
            moved = next != status;
            status = std::move(next);
        }
        if (moved && listener) {
            listener();
        }
    }

    /// Sleep until ``until``; true when stopped meanwhile, or when ``over``
    /// is set (under ``wake_mu``, and ``wake`` told).
    bool sleep_until(Clock::time_point until, const std::atomic<bool>* over = nullptr)
    {
        std::unique_lock lock(wake_mu);
        return wake.wait_until(lock, until, [&] {
            return stopping.load() || (over != nullptr && over->load());
        });
    }

    void run();
    /// ``encoders``, for a codec this computer had not tried yet when the
    /// stream started; ``failed``, and the operator told, when none opens.
    /// Returns as soon as the stream is stopped, the encoders still being
    /// tried.
    void find_encoders();
    /// One connection, from opening it to losing it. Returns why it ended
    /// (empty when stopped, or when it failed for good: see ``failed``).
    std::string session();
    /// The connection, open (a listener's once a receiver has come), or null
    /// with ``why`` saying why (empty when stopped).
    AVIOContext* open_connection(std::string& why);
    /// The first of ``encoders`` that opens at ``bitrate``, and the sound
    /// encoder. False when either cannot, which no retry mends: ``failed``
    /// is set and the operator told.
    bool open_encoders(VideoEncoder& video, AudioEncoder& sound_encoder, EncoderInfo& chosen,
                       int bitrate);
    /// A picture every frame and the sound taken since, until stopped. False
    /// when an encoder or a write fails.
    bool send(VideoEncoder& video, AudioEncoder& sound_encoder,
              const VideoEncoder::PacketSink& video_out, const AudioEncoder::PacketSink& sound_out);
    /// A connection's sound clock, from ``start`` until ``over``: every
    /// 10 ms, the sound the time since ``start`` holds, from ``sound`` into
    /// ``taken``.
    void take_sound(Clock::time_point start, SoundTaken& taken, const std::atomic<bool>& over);
};

void SrtStreamer::Impl::run()
{
    if (encoders.empty()) {
        find_encoders();
    }
    while (!stopping && !failed) {
        const std::string why = session();
        if (stopping || failed) {
            break;
        }
        update([&](StreamStatus& s) {
            s.state = StreamState::Retrying;
            s.error = why;
        });
        if (sleep_until(Clock::now() + k_retry_after)) {
            break;
        }
    }
    if (!failed) {
        update([](StreamStatus& s) {
            s.state = StreamState::Stopped;
            s.error.clear();
        });
    }
}

void SrtStreamer::Impl::find_encoders()
{
    update([&](StreamStatus& s) {
        s.state = settings.mode == SrtMode::Listener ? StreamState::Waiting : StreamState::Connecting;
    });
    // The probe cannot be cut short, and may wait behind another one, so it
    // runs on a thread of its own, which finishes it whether or not this
    // stream is still there to take the answer. It touches only the encoder
    // catalog, which is never destroyed. A stop meanwhile ends this at once.
    std::promise<std::vector<EncoderInfo>> found;
    auto answer = found.get_future();
    std::thread([codec = settings.codec, found = std::move(found)]() mutable {
        found.set_value(usable_encoders(codec));
    }).detach();
    while (answer.wait_for(k_probe_check) != std::future_status::ready) {
        if (stopping) {
            return;
        }
    }
    encoders = answer.get();
    if (encoders.empty()) {
        failed = true;
        update([&](StreamStatus& s) {
            s.state = StreamState::Failed;
            s.error = no_encoder_text(settings.codec);
        });
    }
}

std::string SrtStreamer::Impl::session()
{
    const bool listener_mode = settings.mode == SrtMode::Listener;
    update([&](StreamStatus& s) {
        s.state = listener_mode ? StreamState::Waiting : StreamState::Connecting;
    });
    std::string why;
    AVIOContext* io = open_connection(why);
    if (io == nullptr) {
        return why;
    }

    // Opened only now, so the receiver's first picture is a keyframe.
    const int bitrate = bitrate_kbps(settings, width, height);
    VideoEncoder video;
    AudioEncoder sound_encoder;
    EncoderInfo chosen;
    if (!open_encoders(video, sound_encoder, chosen, bitrate)) {
        avio_closep(&io);
        return {};
    }

    AVFormatContext* mux = nullptr;
    avformat_alloc_output_context2(&mux, nullptr, "mpegts", nullptr);
    AVStream* video_stream = mux != nullptr ? avformat_new_stream(mux, nullptr) : nullptr;
    AVStream* sound_stream = mux != nullptr ? avformat_new_stream(mux, nullptr) : nullptr;
    const auto close_all = [&] {
        if (mux != nullptr) {
            mux->pb = nullptr;
            avformat_free_context(mux);
        }
        avio_closep(&io);
    };
    if (video_stream == nullptr || sound_stream == nullptr) {
        close_all();
        return "Out of memory starting the stream.";
    }
    avcodec_parameters_from_context(video_stream->codecpar, video.context());
    video_stream->time_base = video.context()->time_base;
    avcodec_parameters_from_context(sound_stream->codecpar, sound_encoder.context());
    sound_stream->time_base = sound_encoder.context()->time_base;
    mux->pb = io;
    // Each packet goes out as it is written, not when a buffer fills.
    mux->flags |= AVFMT_FLAG_FLUSH_PACKETS;
    if (const int ret = avformat_write_header(mux, nullptr); ret < 0) {
        close_all();
        return "Could not start the stream: " + av_error_text(ret);
    }

    update([&](StreamStatus& s) {
        s.state = StreamState::Sending;
        s.encoder = chosen;
        s.bitrate_kbps = bitrate;
        s.error.clear();
    });
    pictures_sent = 0;
    audio_frames_sent = 0;
    sound.reset();

    // Told apart from an encoder failing, for what the operator is told.
    bool unsent = false;
    const auto write = [&](AVPacket* packet, AVRational from, AVStream* to) {
        av_packet_rescale_ts(packet, from, to->time_base);
        packet->stream_index = to->index;
        unsent = av_interleaved_write_frame(mux, packet) < 0;
        return !unsent;
    };
    const AVRational video_base = video.context()->time_base;
    const AVRational sound_base = sound_encoder.context()->time_base;
    const VideoEncoder::PacketSink video_out = [&](AVPacket* p) {
        return write(p, video_base, video_stream);
    };
    const AudioEncoder::PacketSink sound_out = [&](AVPacket* p) {
        return write(p, sound_base, sound_stream);
    };
    const bool lost = !send(video, sound_encoder, video_out, sound_out);
    if (!lost) {
        (void)av_write_trailer(mux);
    }
    close_all();
    if (!lost || stopping) {
        return {};
    }
    if (!unsent) {
        return "Encoding failed.";
    }
    return listener_mode ? "The receiver disconnected."
                         : "Lost the connection to " + address_text(settings) + ".";
}

AVIOContext* SrtStreamer::Impl::open_connection(std::string& why)
{
    const bool listener_mode = settings.mode == SrtMode::Listener;
    // Every SRT setting as an option rather than in the URL, so nothing in
    // it (a passphrase) needs escaping.
    AVDictionary* options = nullptr;
    av_dict_set(&options, "mode", listener_mode ? "listener" : "caller", 0);
    av_dict_set(&options, "transtype", "live", 0);
    // FFmpeg takes SRT's latency in microseconds.
    av_dict_set_int(&options, "latency", std::int64_t{latency_ms(settings.preset)} * 1000, 0);
    av_dict_set(&options, "pkt_size", k_srt_packet_size, 0);
    if (!listener_mode) {
        av_dict_set_int(&options, "connect_timeout", k_connect_timeout_ms, 0);
    }
    if (!settings.passphrase.empty()) {
        av_dict_set(&options, "passphrase", settings.passphrase.c_str(), 0);
    }
    // A listener waits on every IPv4 address this computer has.
    const std::string url = listener_mode ? "srt://0.0.0.0:" + std::to_string(settings.port)
                                          : address_text(settings);
    AVIOInterruptCB interrupt{&Impl::interrupted, this};
    AVIOContext* io = nullptr;
    const int ret = avio_open2(&io, url.c_str(), AVIO_FLAG_WRITE, &interrupt, &options);
    av_dict_free(&options);
    if (ret < 0) {
        why.clear();
        if (!stopping) {
            why = listener_mode ? "Could not wait for a receiver on port "
                                      + std::to_string(settings.port) + ": " + av_error_text(ret)
                                : "Could not reach " + address_text(settings) + ": "
                                      + av_error_text(ret);
        }
        return nullptr;
    }
    return io;
}

bool SrtStreamer::Impl::open_encoders(VideoEncoder& video, AudioEncoder& sound_encoder,
                                      EncoderInfo& chosen, int bitrate)
{
    VideoEncoder::Config config;
    config.width = width;
    config.height = height;
    config.frame_rate = k_frame_rate;
    config.bitrate_kbps = bitrate;
    config.keyframe_interval = keyframe_seconds(settings.preset) * k_frame_rate;
    config.low_delay = settings.preset == StreamPreset::LowDelay;
    std::string error;
    for (const EncoderInfo& candidate : encoders) {
        if (video.open(candidate.name, config, error)) {
            chosen = candidate;
            break;
        }
    }
    if (video.is_open() && sound_encoder.open(error)) {
        return true;
    }
    failed = true;
    update([&](StreamStatus& s) {
        s.state = StreamState::Failed;
        s.error = error;
    });
    return false;
}

bool SrtStreamer::Impl::send(VideoEncoder& video, AudioEncoder& sound_encoder,
                             const VideoEncoder::PacketSink& video_out,
                             const AudioEncoder::PacketSink& sound_out)
{
    // Black until the first picture comes.
    std::vector<std::uint8_t> picture_now(static_cast<std::size_t>(width) * height * 4, 0);
    int now_width = width;
    int now_height = height;
    int now_stride = width * 4;
    std::uint64_t seen = 0;
    std::vector<float> sound_now;

    const auto period = std::chrono::nanoseconds(1'000'000'000LL / k_frame_rate);
    const Clock::time_point start = Clock::now();
    SoundTaken taken;
    std::atomic<bool> over{false};
    std::thread sound_clock([&] { take_sound(start, taken, over); });
    std::int64_t frame = 0;
    bool lost = false;
    while (!stopping) {
        const Clock::time_point due = start + frame * period;
        if (sleep_until(due)) {
            break;
        }
        const Clock::time_point now = Clock::now();
        if (now - due > k_resync_after) {
            frame = (now - start) / period;
        }
        {
            std::lock_guard lock(picture_mu);
            if (picture_serial != seen && !picture.empty()) {
                picture_now.swap(picture);
                now_width = picture_width;
                now_height = picture_height;
                now_stride = picture_stride;
                seen = picture_serial;
            }
        }
        if (!video.encode(picture_now.data(), now_stride, now_width, now_height, frame,
                          video_out)) {
            lost = true;
            break;
        }
        ++pictures_sent;

        // The sound taken since the last picture.
        {
            std::lock_guard lock(taken.mu);
            sound_now.swap(taken.frames);
        }
        if (!sound_now.empty()) {
            if (!sound_encoder.add(sound_now.data(), sound_now.size() / audio::k_channels,
                                   sound_out)) {
                lost = true;
                break;
            }
            sound_now.clear();
            audio_frames_sent = sound_encoder.frames_encoded();
        }
        ++frame;
    }
    {
        std::lock_guard lock(wake_mu);
        over = true;
    }
    wake.notify_all();
    sound_clock.join();
    return !lost;
}

void SrtStreamer::Impl::take_sound(Clock::time_point start, SoundTaken& taken,
                                   const std::atomic<bool>& over)
{
    std::vector<float> block;
    std::int64_t pulled = 0;
    std::int64_t tick = 0;
    for (;;) {
        ++tick;
        const Clock::time_point due = start + tick * k_sound_period;
        if (sleep_until(due, &over)) {
            return;
        }
        const Clock::time_point now = Clock::now();
        if (now - due > k_resync_after) {
            tick = (now - start) / k_sound_period;
        }
        // The sound the time since the start holds: picture and sound keep
        // to one clock.
        const std::int64_t due_frames =
            std::chrono::duration_cast<std::chrono::microseconds>(now - start).count()
            * audio::k_sample_rate / 1'000'000;
        const std::int64_t count = std::min(due_frames - pulled, k_max_audio_step);
        if (count <= 0) {
            continue;
        }
        block.resize(static_cast<std::size_t>(count) * audio::k_channels);
        sound.pull(block.data(), static_cast<std::size_t>(count));
        pulled += count;
        // Builds up only while a write holds the sending loop up, which SRT
        // bounds (a receiver silent for 5 s is gone).
        std::lock_guard lock(taken.mu);
        taken.frames.insert(taken.frames.end(), block.begin(), block.end());
    }
}

SrtStreamer::SrtStreamer()
    : impl_(std::make_unique<Impl>())
{
}

SrtStreamer::~SrtStreamer()
{
    stop();
}

void SrtStreamer::set_status_listener(StatusListener listener)
{
    impl_->listener = std::move(listener);
}

bool SrtStreamer::start(const StreamSettings& settings, int width, int height,
                        std::string& error)
{
    error = validate(settings);
    if (error.empty()) {
        error = missing_to_send(settings);
    }
    if (!error.empty()) {
        return false;
    }
    if (!srt_supported()) {
        error = "This build of FFmpeg cannot send SRT.";
        return false;
    }
    if (width < 2 || height < 2) {
        error = "Nothing to stream: the picture has no size.";
        return false;
    }
    // Those not tried yet are tried on the stream's thread (Impl::run).
    auto encoders = known_encoders(settings.codec);
    if (encoders && encoders->empty()) {
        error = no_encoder_text(settings.codec);
        return false;
    }
    stop();
    Impl& d = *impl_;
    d.settings = settings;
    d.width = width & ~1;
    d.height = height & ~1;
    d.encoders = encoders ? std::move(*encoders) : std::vector<EncoderInfo>{};
    d.stopping = false;
    d.failed = false;
    {
        std::lock_guard lock(d.status_mu);
        d.status = StreamStatus{};
    }
    d.thread = std::thread([&d] { d.run(); });
    return true;
}

void SrtStreamer::stop()
{
    Impl& d = *impl_;
    if (!d.thread.joinable()) {
        return;
    }
    {
        std::lock_guard lock(d.wake_mu);
        d.stopping = true;
    }
    d.wake.notify_all();
    d.thread.join();
}

bool SrtStreamer::running() const noexcept
{
    return impl_->thread.joinable() && !impl_->stopping.load();
}

void SrtStreamer::submit_picture(const std::uint8_t* pixels, int width, int height, int stride)
{
    if (pixels == nullptr || width <= 0 || height <= 0 || stride < width * 4) {
        return;
    }
    Impl& d = *impl_;
    const auto bytes = static_cast<std::size_t>(stride) * static_cast<std::size_t>(height);
    std::lock_guard lock(d.picture_mu);
    // The buffer the streaming thread last gave back is reused.
    d.picture.resize(bytes);
    std::memcpy(d.picture.data(), pixels, bytes);
    d.picture_width = width;
    d.picture_height = height;
    d.picture_stride = stride;
    ++d.picture_serial;
}

void SrtStreamer::push_audio(const float* frames, std::size_t count)
{
    impl_->sound.push(frames, count);
}

StreamStatus SrtStreamer::status() const
{
    std::lock_guard lock(impl_->status_mu);
    return impl_->status;
}

std::int64_t SrtStreamer::pictures_sent() const noexcept
{
    return impl_->pictures_sent.load();
}

std::int64_t SrtStreamer::audio_frames_sent() const noexcept
{
    return impl_->audio_frames_sent.load();
}

}  // namespace seventhslide::stream
