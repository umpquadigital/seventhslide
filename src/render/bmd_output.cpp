#include <seventhslide/render/bmd_output.hpp>

#include "bmd_internal.hpp"

#include <seventhslide/render/latest_frame.hpp>
#include <seventhslide/render/pixel_pack.hpp>

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/audio/drift_buffer.hpp>

#include <QColor>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace seventhslide::render {
namespace {

using bmd::ComRef;

/// Windows wants the DeckLink objects in an MTA; the worker thread is that
/// apartment. A no-op everywhere else.
struct ComScope {
#if defined(_WIN32)
    ComScope() { ::CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() { ::CoUninitialize(); }
#else
    ComScope() = default;
#endif
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

/// Pick a mode when the operator has not chosen one: the most ordinary
/// progressive 1080 the card offers, else the first mode it lists.
[[nodiscard]] std::optional<BmdDisplayModeInfo> default_mode(
    const std::vector<BmdDisplayModeInfo>& modes)
{
    if (modes.empty()) {
        return std::nullopt;
    }
    for (const auto& m : modes) {
        if (m.width == 1920 && m.height == 1080 && !m.interlaced) {
            return m;
        }
    }
    return modes.front();
}

/// Write ``image`` into a card frame of ``rows`` rows of ``stride`` bytes, as
/// BGRA or as 8-bit 4:2:2 in the matrix of a picture ``rows`` high. False when
/// it could not.
bool fill_card_frame(const QImage& image, bool bgra, std::uint8_t* dst, int stride, int rows)
{
    if (!bgra) {
        return pack_yuv422(image, Yuv422Order::Uyvy, video_yuv_matrix(rows), dst, stride);
    }
    // Qt's ARGB32 is B,G,R,A in memory on little-endian - exactly
    // bmdFormat8BitBGRA, so this is a stride-fixing copy, not a conversion.
    const QImage src = image.convertToFormat(QImage::Format_ARGB32);
    if (src.isNull()) {
        return false;
    }
    const int copy_rows = std::min(rows, src.height());
    const qsizetype copy_bytes = std::min(qsizetype(stride), qsizetype(src.width()) * 4);
    // DeckLink pads rows, so the tail of each row - and any row the image is
    // too short to cover - has to be cleared, not left as whatever the frame
    // held.
    std::memset(dst, 0, size_t(qsizetype(stride) * rows));
    for (int y = 0; y < copy_rows; ++y) {
        std::memcpy(dst + qsizetype(stride) * y, src.constScanLine(y), size_t(copy_bytes));
    }
    return true;
}

/// Top the card's sound up from ``drift`` (see bmd_audio_top_up).
void write_audio(IDeckLinkOutput& output, audio::DriftBuffer& drift, std::uint32_t target,
                 std::vector<float>& floats, std::vector<std::int32_t>& ints)
{
    std::uint32_t buffered = 0;
    if (output.GetBufferedAudioSampleFrameCount(&buffered) != S_OK) {
        return;
    }
    const std::uint32_t frames = bmd_audio_top_up(buffered, target);
    if (frames == 0) {
        return;
    }
    const std::size_t samples = std::size_t(frames) * audio::k_channels;
    floats.resize(samples);
    ints.resize(samples);
    drift.pull(floats.data(), frames);
    bmd_audio_pack(floats.data(), samples, ints.data());
    std::uint32_t written = 0;
    output.WriteAudioSamplesSync(ints.data(), frames, &written);
}

}  // namespace

struct BmdSink::Impl {
    QString mode_key;
    bool alpha = false;

    // Resolved mode, cached against the device + key it was resolved for.
    mutable std::optional<BmdDisplayModeInfo> mode_cache;
    mutable QString cache_device;
    mutable QString cache_key;

    // Published by the worker once the device is open, read by the GUI thread
    // (``deliver`` sends nothing before).
    std::atomic<int> row_bytes{0};
    std::atomic<int> frame_rows{0};
    std::atomic<bool> keying{false};
    /// The card took audio output: it is playing what ``drift`` holds.
    std::atomic<bool> audio_ok{false};

    /// The output's mix, from the mixer's clock to the card's.
    audio::DriftBuffer drift;

    std::thread worker;
    /// The newest picture, for the worker to write into the card's frame; a
    /// keepalive shows the frame again as it is.
    LatestFrame frames;
    /// The open handshake: the worker says how opening went.
    std::mutex mu;
    std::condition_variable cv;
    bool stop = false;
    bool open_done = false;
    bool open_ok = false;
    QString open_error;
};

BmdSink::BmdSink(QObject* parent)
    : OffscreenSink(parent)
    , d_(std::make_unique<Impl>())
{
}

BmdSink::~BmdSink()
{
    stop();
}

void BmdSink::set_mode_key(QString key)
{
    d_->mode_key = std::move(key);
    d_->mode_cache.reset();
}

QString BmdSink::mode_key() const
{
    return d_->mode_key;
}

void BmdSink::set_alpha_enabled(bool on)
{
    if (d_->alpha == on) {
        return;
    }
    d_->alpha = on;
    mark_dirty();
}

bool BmdSink::alpha_enabled() const noexcept
{
    return d_->alpha;
}

bool BmdSink::keying_active() const noexcept
{
    return d_->keying.load();
}

void BmdSink::set_audio(audio::Mixer* mixer, std::string output_id, bool on)
{
    if (!link_.set(mixer, std::move(output_id), on)) {
        return;
    }
    // Left the mixer if it had joined: what is buffered was the old mix.
    d_->drift.reset();
    if (d_->audio_ok) {
        link_.join();
    }
}

void BmdSink::push(const float* frames, std::size_t count)
{
    d_->drift.push(frames, count);
}

OffscreenSink::Status BmdSink::status() const
{
    Status s;
    s.mode_name = active_mode().name;
    s.keying = keying_active();
    return s;
}

QColor BmdSink::letterbox_color() const
{
    return d_->alpha ? QColor(Qt::transparent) : QColor(Qt::black);
}

BmdDisplayModeInfo BmdSink::resolve_mode() const
{
    const QString device = target();
    if (d_->mode_cache && d_->cache_device == device && d_->cache_key == d_->mode_key) {
        return *d_->mode_cache;
    }
    BmdDisplayModeInfo chosen;
    const auto modes = bmd_list_display_modes(device);
    if (!modes.empty()) {
        bool found = false;
        if (!d_->mode_key.isEmpty()) {
            for (const auto& m : modes) {
                if (m.key == d_->mode_key) {
                    chosen = m;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            // A persisted mode the card no longer offers (profile change, or a
            // different card in the same slot) falls back rather than failing:
            // better a running output in a sane mode than a dark one.
            if (const auto fallback = default_mode(modes)) {
                chosen = *fallback;
            }
        }
    }
    d_->mode_cache = chosen;
    d_->cache_device = device;
    d_->cache_key = d_->mode_key;
    return chosen;
}

BmdDisplayModeInfo BmdSink::active_mode() const
{
    return resolve_mode();
}

QSize BmdSink::negotiate_size(QSize want) const
{
    const BmdDisplayModeInfo mode = resolve_mode();
    if (mode.width > 0 && mode.height > 0) {
        return {mode.width, mode.height};
    }
    // No device / no modes: keep the base's behaviour so the sink can still be
    // constructed and report a clean error from on_start.
    return OffscreenSink::negotiate_size(want);
}

int BmdSink::tick_interval_ms() const
{
    const BmdDisplayModeInfo mode = resolve_mode();
    if (mode.time_scale > 0 && mode.frame_duration > 0) {
        return mode.tick_interval_ms();
    }
    return OffscreenSink::tick_interval_ms();
}

bool BmdSink::on_resize(QSize size)
{
    // The mode is fixed for the life of the output. Refusing keeps the
    // previous format rather than dropping the feed out from under a live
    // audience - see OffscreenSink::follow_size.
    Q_UNUSED(size);
    return false;
}

bool BmdSink::on_start(QSize size)
{
    Q_UNUSED(size);
    if (!bmd_runtime_available()) {
        set_last_error(bmd_missing_message());
        return false;
    }
    if (target().isEmpty()) {
        set_last_error(bmd_no_device_message());
        return false;
    }
    const BmdDisplayModeInfo mode = resolve_mode();
    if (mode.width <= 0 || mode.height <= 0) {
        set_last_error(bmd_no_device_message());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(d_->mu);
        d_->stop = false;
        d_->open_done = false;
        d_->open_ok = false;
        d_->open_error.clear();
    }
    d_->frames.reset();
    d_->row_bytes = 0;
    d_->frame_rows = 0;
    d_->keying = false;

    d_->worker = std::thread([this, mode]() { worker_loop(mode); });

    // Opening happens on the worker so the DeckLink objects live in the
    // apartment that drives them, but start() is synchronous, so wait for the
    // verdict before reporting success.
    std::unique_lock<std::mutex> lock(d_->mu);
    d_->cv.wait(lock, [this]() { return d_->open_done; });
    const bool ok = d_->open_ok;
    const QString err = d_->open_error;
    lock.unlock();

    if (!ok) {
        set_last_error(err.isEmpty() ? bmd_no_device_message() : err);
        {
            std::lock_guard<std::mutex> guard(d_->mu);
            d_->stop = true;
        }
        d_->cv.notify_all();
        if (d_->worker.joinable()) {
            d_->worker.join();
        }
        return false;
    }
    set_output_size(QSize(mode.width, mode.height));
    if (d_->audio_ok) {
        link_.join();
    }
    return true;
}

void BmdSink::on_stop()
{
    // The sound first: nothing pushes once the writer has gone.
    link_.leave();
    {
        std::lock_guard<std::mutex> lock(d_->mu);
        d_->stop = true;
    }
    d_->cv.notify_all();
    d_->frames.stop();
    if (d_->worker.joinable()) {
        d_->worker.join();
    }
    d_->frames.reset();
    d_->row_bytes = 0;
    d_->frame_rows = 0;
    d_->keying = false;
    d_->drift.reset();
}

void BmdSink::deliver(const QImage& image, bool changed)
{
    if (d_->row_bytes.load() <= 0 || d_->frame_rows.load() <= 0 || image.isNull()) {
        return;
    }
    // The card is fed continuously, but a static slide is packed once: a
    // keepalive shows the frame again.
    d_->frames.put(changed ? image : QImage());
}

void BmdSink::worker_loop(BmdDisplayModeInfo mode)
{
    [[maybe_unused]] ComScope com;

    ComRef<IDeckLink> device;
    ComRef<IDeckLinkOutput> output;
    ComRef<IDeckLinkMutableVideoFrame> frame;
    ComRef<IDeckLinkKeyer> keyer;
    QString error;
    bool ok = false;
    bool audio_on_card = false;
    // The card takes BGRA (else 8-bit 4:2:2).
    bool bgra = true;

    do {
        device = bmd::find_device(target());
        if (!device) {
            error = bmd_no_device_message();
            break;
        }
        IDeckLinkOutput* raw_output = nullptr;
        if (device->QueryInterface(IID_IDeckLinkOutput, reinterpret_cast<void**>(&raw_output))
                != S_OK
            || raw_output == nullptr) {
            error = QStringLiteral("This Blackmagic device has no video output.");
            break;
        }
        output = ComRef<IDeckLinkOutput>(raw_output);

        const BMDDisplayMode display_mode = bmd::display_mode_from_key(mode.key);

        // BGRA first: it needs no colour conversion from what we render, and
        // it is the only way to carry alpha for the keyer. Not every output
        // card accepts it, so fall back to 8-bit YUV.
        BMDPixelFormat pixel_format = bmdFormat8BitBGRA;
        BMDDisplayModeSupport support = bmdDisplayModeNotSupported;
        if (output->DoesSupportVideoMode(display_mode, pixel_format, bmdVideoOutputFlagDefault,
                                         &support, nullptr)
                != S_OK
            || support == bmdDisplayModeNotSupported) {
            pixel_format = bmdFormat8BitYUV;
            support = bmdDisplayModeNotSupported;
            if (output->DoesSupportVideoMode(display_mode, pixel_format,
                                             bmdVideoOutputFlagDefault, &support, nullptr)
                    != S_OK
                || support == bmdDisplayModeNotSupported) {
                error = QStringLiteral("This device cannot output %1.").arg(mode.name);
                break;
            }
        }

        if (output->EnableVideoOutput(display_mode, bmdVideoOutputFlagDefault) != S_OK) {
            // The usual cause by far: something else already owns this output.
            error = QStringLiteral(
                        "Could not open the Blackmagic output. Another application may "
                        "already be using this device.");
            break;
        }

        const int bytes_per_pixel = pixel_format == bmdFormat8BitBGRA ? 4 : 2;
        if (output->CreateVideoFrame(mode.width, mode.height, mode.width * bytes_per_pixel,
                                     pixel_format, bmdFrameFlagDefault, frame.receive())
                != S_OK
            || !frame) {
            error = QStringLiteral("Could not allocate a Blackmagic output frame.");
            output->DisableVideoOutput();
            break;
        }

        if (d_->alpha && pixel_format == bmdFormat8BitBGRA) {
            IDeckLinkKeyer* raw_keyer = nullptr;
            if (device->QueryInterface(IID_IDeckLinkKeyer, reinterpret_cast<void**>(&raw_keyer))
                    == S_OK
                && raw_keyer != nullptr) {
                keyer = ComRef<IDeckLinkKeyer>(raw_keyer);
                // Internal keying: the graphic is keyed over the card's own
                // input. External keying needs a second output for the matte
                // and is a separate operator choice.
                if (keyer->Enable(false) == S_OK) {
                    keyer->SetLevel(255);
                    d_->keying = true;
                } else {
                    keyer.reset();
                }
            }
        }

        // Sound beside the pictures. A card that will not take it leaves the
        // pictures running alone.
        audio_on_card = output->EnableAudioOutput(bmdAudioSampleRate48kHz,
                                                  bmdAudioSampleType32bitInteger,
                                                  audio::k_channels,
                                                  bmdAudioOutputStreamContinuous)
                        == S_OK;
        d_->audio_ok = audio_on_card;

        bgra = pixel_format == bmdFormat8BitBGRA;
        d_->row_bytes = int(frame->GetRowBytes());
        d_->frame_rows = int(frame->GetHeight());
        ok = true;
    } while (false);

    {
        std::lock_guard<std::mutex> lock(d_->mu);
        d_->open_done = true;
        d_->open_ok = ok;
        d_->open_error = error;
    }
    d_->cv.notify_all();
    if (!ok) {
        return;
    }

    // A picture late from the GUI must not leave the sound short: without
    // one, the writer still wakes once a frame to top the sound up.
    const auto frame_period = std::chrono::milliseconds(std::max(1, mode.tick_interval_ms()));
    const std::uint32_t audio_target = bmd_audio_target_frames(mode);
    std::vector<float> audio_floats;
    std::vector<std::int32_t> audio_ints;
    const int stride = int(frame->GetRowBytes());
    const int rows = int(frame->GetHeight());
    // The frame holds a picture: a keepalive shows it again.
    bool filled = false;
    while (true) {
        auto next = d_->frames.wait(frame_period);
        if (next.stopped) {
            break;
        }
        QImage image = std::move(next.image);
        const bool show = next.handed;
        void* bytes = nullptr;
        if (!image.isNull() && frame->GetBytes(&bytes) == S_OK && bytes != nullptr) {
            filled = fill_card_frame(image, bgra, static_cast<std::uint8_t*>(bytes), stride, rows);
            image = QImage();  // the presenter's buffer, given back at once
        }
        if (show && filled) {
            output->DisplayVideoFrameSync(frame.get());
        }
        if (audio_on_card) {
            write_audio(*output.get(), d_->drift, audio_target, audio_floats, audio_ints);
        }
    }

    if (keyer) {
        keyer->Disable();
    }
    frame.reset();
    if (output) {
        if (audio_on_card) {
            output->DisableAudioOutput();
        }
        output->DisableVideoOutput();
    }
    d_->audio_ok = false;
    d_->keying = false;
    d_->row_bytes = 0;
    d_->frame_rows = 0;
}

}  // namespace seventhslide::render
