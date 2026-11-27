#include <seventhslide/render/ndi_output.hpp>

#include <seventhslide/render/latest_frame.hpp>
#include <seventhslide/render/ndi_runtime.hpp>

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/audio/drift_buffer.hpp>

#include <QColor>
#include <QTimer>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <utility>

namespace seventhslide::render {
namespace {

/// Receivers and tally are asked for about twice a second.
constexpr int k_status_poll_ms = 500;

/// NDI timecodes count 100 ns.
constexpr std::int64_t k_timecode_per_sec = 10'000'000;

/// One block's time, by which the sound is sent.
constexpr auto k_audio_period = std::chrono::microseconds(
    static_cast<long long>(audio::k_block_frames) * 1'000'000 / audio::k_sample_rate);

/// A sender this far behind (a suspended laptop) carries on from now rather
/// than racing to catch up; timecodes this far from the time are set again.
constexpr auto k_audio_resync = std::chrono::milliseconds(100);

/// Now, as the timecodes NDI synthesizes count it: 100 ns since the Unix
/// epoch, so the sound's timecodes and the pictures' agree.
std::int64_t timecode_now()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
               .count()
           / 100;
}

}  // namespace

/// The sound's way out. The mixer's clock pushes blocks as its device asks
/// for them, which is unevenly (several at once, then none, as the device's
/// buffer empties); sent like that, a receiver's timing would stretch and
/// squeeze the sound and it would crackle. So a DriftBuffer takes them, and
/// a thread of the sink's own sends one block every 10 ms by the system
/// clock, de-interleaved, as NDI audio frames. Each frame's timecode follows
/// the last by exactly its length rather than by when it happened to go out.
struct NdiSink::AudioPath {
    AudioPath(const ndi::Api& runtime, ndi::send_instance_t to)
        : api(runtime)
        , sender(to)
        , thread([this] { run(); })
    {
    }

    ~AudioPath()
    {
        {
            std::lock_guard lock(mu);
            stop = true;
        }
        wake.notify_all();
        thread.join();
    }

    AudioPath(const AudioPath&) = delete;
    AudioPath& operator=(const AudioPath&) = delete;

    void push(const float* frames, std::size_t count) { drift.push(frames, count); }

    void run()
    {
        using Clock = std::chrono::steady_clock;
        constexpr std::size_t n = audio::k_block_frames;
        std::array<float, n * audio::k_channels> interleaved{};
        std::array<float, n * audio::k_channels> planar{};
        constexpr auto resync =
            std::chrono::duration_cast<std::chrono::nanoseconds>(k_audio_resync).count() / 100;
        std::int64_t anchor = 0;
        std::uint64_t since_anchor = 0;
        bool anchored = false;
        auto next = Clock::now();
        std::unique_lock lock(mu);
        for (;;) {
            next += k_audio_period;
            if (wake.wait_until(lock, next, [this] { return stop; })) {
                return;
            }
            if (Clock::now() - next > k_audio_resync) {
                next = Clock::now();
            }
            lock.unlock();

            drift.pull(interleaved.data(), n);
            for (std::size_t i = 0; i < n; ++i) {
                for (int c = 0; c < audio::k_channels; ++c) {
                    planar[c * n + i] = interleaved[i * audio::k_channels + c];
                }
            }
            const std::int64_t now = timecode_now();
            std::int64_t timecode = anchor
                                    + static_cast<std::int64_t>(since_anchor) * k_timecode_per_sec
                                          / audio::k_sample_rate;
            if (!anchored || std::llabs(timecode - now) > resync) {
                // The first frame, or the clock has jumped (or this thread
                // stalled): start counting again from now.
                anchor = now;
                since_anchor = 0;
                anchored = true;
                timecode = now;
            }
            ndi::audio_frame_v3_t frame;
            frame.sample_rate = audio::k_sample_rate;
            frame.no_channels = audio::k_channels;
            frame.no_samples = static_cast<std::int32_t>(n);
            frame.timecode = timecode;
            frame.FourCC = ndi::k_fourcc_fltp;
            frame.p_data = reinterpret_cast<std::uint8_t*>(planar.data());
            frame.channel_stride_in_bytes = static_cast<std::int32_t>(n * sizeof(float));
            // Sent at once: the runtime is done with the buffer on return.
            api.send_send_audio_v3(sender, &frame);
            since_anchor += n;
            sent += n;

            lock.lock();
        }
    }

    const ndi::Api& api;
    ndi::send_instance_t sender;
    /// Between the mixer's clock and the sending thread's.
    audio::DriftBuffer drift;
    std::mutex mu;
    std::condition_variable wake;
    bool stop = false;
    std::atomic<std::uint64_t> sent{0};
    std::thread thread;
};

/// The pictures' way out: each normalized to NDI's BGRX or BGRA and sent on a
/// thread of the sink's own. The newest picture wins; a keepalive sends the
/// one before again.
struct NdiSink::VideoPath {
    VideoPath(const ndi::Api& runtime, ndi::send_instance_t to)
        : api(runtime)
        , sender(to)
        , thread([this] { run(); })
    {
    }

    ~VideoPath()
    {
        frames.stop();
        thread.join();
        // Flushing with a null frame releases the buffer the last async send
        // is still holding, so the images below are safe to free.
        api.send_send_video_async_v2(sender, nullptr);
    }

    VideoPath(const VideoPath&) = delete;
    VideoPath& operator=(const VideoPath&) = delete;

    void run()
    {
        for (;;) {
            auto next = frames.wait();
            if (next.stopped) {
                return;
            }
            if (!next.image.isNull()) {
                // The alpha setting as it is now. Changing it marks the sink
                // dirty, so the next picture is drawn for the new setting.
                const bool keyed = alpha.load();
                // Into the buffer NDI is not reading. ARGB32 (not
                // premultiplied) is NDI's BGRA.
                QImage converted = next.image.convertToFormat(keyed ? QImage::Format_ARGB32
                                                                    : QImage::Format_RGB32);
                next.image = QImage();  // the presenter's buffer, given back at once
                // convertToFormat is a shallow copy when the format already
                // matches; the two slots must never alias each other.
                if (!buffers[index].isNull() && converted.constBits() == buffers[index].constBits()) {
                    converted = converted.copy();
                }
                index ^= 1;
                buffers[index] = std::move(converted);
                alphas[index] = keyed;
            }
            const QImage& buf = buffers[index];
            if (buf.isNull()) {
                continue;
            }
            ndi::video_frame_v2_t frame;
            frame.xres = buf.width();
            frame.yres = buf.height();
            frame.FourCC = alphas[index] ? ndi::k_fourcc_bgra : ndi::k_fourcc_bgrx;
            frame.frame_rate_N = k_ndi_frame_rate_n;
            frame.frame_rate_D = k_ndi_frame_rate_d;
            frame.picture_aspect_ratio = 0.0F;  // 0 => square pixels, from xres/yres
            frame.frame_format_type = ndi::k_format_progressive;
            frame.timecode = ndi::k_timecode_synthesize;
            frame.p_data = const_cast<std::uint8_t*>(buf.constBits());
            frame.line_stride_in_bytes = static_cast<std::int32_t>(buf.bytesPerLine());
            frame.p_metadata = nullptr;
            frame.timestamp = 0;
            api.send_send_video_async_v2(sender, &frame);
        }
    }

    const ndi::Api& api;
    ndi::send_instance_t sender;
    /// What the window's thread hands over, and whether to send it keyed.
    LatestFrame frames;
    std::atomic<bool> alpha{false};
    /// The sending thread's: ping-pong for the in-flight async send.
    QImage buffers[2];
    bool alphas[2] = {false, false};
    int index = 0;
    std::thread thread;
};

QString ndi_sanitize_source_name(const QString& name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        // NDI source names travel over mDNS. Control characters, and the
        // separators the runtime uses to split machine/source names, break
        // discovery on some receivers. Substitute a space instead of dropping
        // the character so word boundaries survive ("a\tb" -> "a b"), then let
        // simplified() collapse the runs.
        const bool safe = c.isPrint() && c != QLatin1Char('(') && c != QLatin1Char(')')
                          && c != QLatin1Char(',');
        out.append(safe ? c : QLatin1Char(' '));
    }
    return out.simplified();
}

QString ndi_default_source_name(const QString& output_name)
{
    const QString clean = ndi_sanitize_source_name(output_name);
    if (clean.isEmpty()) {
        return QStringLiteral("SeventhSlide");
    }
    return QStringLiteral("SeventhSlide %1").arg(clean);
}

NdiSink::NdiSink(QObject* parent)
    : OffscreenSink(parent)
    , status_timer_(new QTimer(this))
{
    status_timer_->setInterval(k_status_poll_ms);
    connect(status_timer_, &QTimer::timeout, this, [this] { poll_status(); });
}

NdiSink::~NdiSink()
{
    stop();
}

void NdiSink::set_audio(audio::Mixer* mixer, std::string output_id, bool on)
{
    if (!link_.set(mixer, std::move(output_id), on)) {
        return;
    }
    // The link has left the mixer; the sending thread stops here.
    detach_audio();
    attach_audio();
}

std::uint64_t NdiSink::audio_frames_sent() const noexcept
{
    return audio_ ? audio_->sent.load() : 0;
}

void NdiSink::push(const float* frames, std::size_t count)
{
    // Only between add_sink and remove_sink, so audio_ is there.
    audio_->push(frames, count);
}

void NdiSink::attach_audio()
{
    if (audio_ || sender_ == nullptr || api_ == nullptr || !link_.wanted()
        || api_->send_send_audio_v3 == nullptr) {
        return;
    }
    audio_ = std::make_unique<AudioPath>(*api_, sender_);
    link_.join();
}

void NdiSink::detach_audio()
{
    if (!audio_) {
        return;
    }
    // Waits out a block in progress: after it, push is not called again.
    link_.leave();
    audio_.reset();
}

QColor NdiSink::letterbox_color() const
{
    return alpha_ ? QColor(Qt::transparent) : QColor(Qt::black);
}

void NdiSink::set_alpha_enabled(bool on)
{
    if (alpha_ == on) {
        return;
    }
    alpha_ = on;
    // Force a re-render: the letterbox color and the pixel format both change.
    mark_dirty();
}

OffscreenSink::Status NdiSink::status() const
{
    Status s;
    s.receivers = connections_;
    s.on_program = on_program_;
    s.on_preview = on_preview_;
    return s;
}

bool NdiSink::on_start(QSize size)
{
    Q_UNUSED(size);
    api_ = ndi_api();
    if (api_ == nullptr) {
        set_last_error(ndi_missing_message());
        return false;
    }
    const QString name = ndi_sanitize_source_name(target());
    if (name.isEmpty()) {
        set_last_error(QStringLiteral("This NDI destination needs a source name."));
        return false;
    }
    name_utf8_ = name.toUtf8();

    ndi::send_create_t create;
    create.p_ndi_name = name_utf8_.constData();
    create.p_groups = nullptr;
    // Our own ~30 fps timer is the clock. Letting NDI clock the stream would
    // pace sends by blocking, and we deliver from the GUI thread.
    create.clock_video = false;
    create.clock_audio = false;

    sender_ = api_->send_create(&create);
    if (sender_ == nullptr) {
        set_last_error(
            QStringLiteral("Could not start the NDI source \"%1\". Another sender may "
                           "already be using that name.")
                .arg(name));
        name_utf8_.clear();
        return false;
    }
    connections_ = 0;
    on_program_ = false;
    on_preview_ = false;
    status_timer_->start();
    pictures_ = std::make_unique<VideoPath>(*api_, sender_);
    attach_audio();
    return true;
}

void NdiSink::on_stop()
{
    status_timer_->stop();
    // Sound and pictures stop first: their threads send on the sender
    // destroyed below.
    detach_audio();
    pictures_.reset();
    if (api_ != nullptr && sender_ != nullptr) {
        api_->send_destroy(sender_);
    }
    sender_ = nullptr;
    api_ = nullptr;
    name_utf8_.clear();
    connections_ = -1;
    on_program_ = false;
    on_preview_ = false;
}

void NdiSink::deliver(const QImage& image, bool changed)
{
    if (pictures_ && !image.isNull()) {
        pictures_->alpha = alpha_;
        pictures_->frames.put(changed ? image : QImage());
    }
}

void NdiSink::poll_status()
{
    if (api_ == nullptr || sender_ == nullptr) {
        return;
    }
    const int connections = connections_;
    const bool on_program = on_program_;
    const bool on_preview = on_preview_;
    if (api_->send_get_no_connections != nullptr) {
        // Timeout 0: never block the GUI thread.
        connections_ = api_->send_get_no_connections(sender_, 0);
    }
    if (api_->send_get_tally != nullptr) {
        ndi::tally_t tally;
        if (api_->send_get_tally(sender_, &tally, 0)) {
            on_program_ = tally.on_program;
            on_preview_ = tally.on_preview;
        }
    }
    if (connections != connections_ || on_program != on_program_ || on_preview != on_preview_) {
        emit statusChanged();
    }
}

}  // namespace seventhslide::render
