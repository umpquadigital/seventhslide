// DeckLink where its SDK is not wired up yet (macOS, Windows): the driver is
// never found, so Blackmagic shows greyed out with a reason, there are no
// devices and no inputs, and a sink fails to start, as on a Linux machine
// without Desktop Video. What needs no driver is bmd_common.cpp's.
//
// Wiring a system up means its own SDK headers and loader in
// third_party/decklink (macOS: a CFBundle dispatch; Windows: COM from
// DeckLinkAPI.idl), building bmd_runtime/bmd_input/bmd_output.cpp there in
// place of this file, and bmd_internal.hpp's adopt_string for its strings.

#include <seventhslide/render/bmd_input.hpp>
#include <seventhslide/render/bmd_output.hpp>
#include <seventhslide/render/bmd_runtime.hpp>

#include <QColor>

#include <utility>

namespace seventhslide::render {

bool bmd_runtime_available()
{
    return false;
}

QString bmd_runtime_version()
{
    return {};
}

QString bmd_runtime_path()
{
    return {};
}

void bmd_runtime_refresh() {}

QString bmd_missing_message()
{
    return QStringLiteral(
        "Blackmagic devices are not supported on this system yet: SeventhSlide uses "
        "Desktop Video's devices on Linux only so far.");
}

QString bmd_no_device_message()
{
    return QStringLiteral(
        "No Blackmagic output device found. Connect a DeckLink or UltraStudio device, "
        "then reopen this dialog.");
}

std::vector<BmdDeviceInfo> bmd_list_devices()
{
    return {};
}

std::vector<BmdDisplayModeInfo> bmd_list_display_modes(const QString& device_id)
{
    Q_UNUSED(device_id);
    return {};
}

std::vector<BmdDeviceInfo> bmd_list_input_devices()
{
    return {};
}

struct BmdReceiver::Impl {
    audio::StreamSource own_sound;
    audio::StreamSource* sound = &own_sound;
};

BmdReceiver::BmdReceiver(QString device_name, FrameReady on_frame, audio::StreamSource* sound)
    : impl_(std::make_unique<Impl>())
{
    Q_UNUSED(device_name);
    Q_UNUSED(on_frame);
    if (sound != nullptr) {
        impl_->sound = sound;
    }
}

BmdReceiver::~BmdReceiver() = default;

bool BmdReceiver::available() const noexcept
{
    return false;
}

QString BmdReceiver::problem() const
{
    return bmd_missing_message();
}

QImage BmdReceiver::frame() const
{
    return {};
}

BmdReceiver::Stats BmdReceiver::stats() const
{
    return {};
}

audio::StreamSource& BmdReceiver::sound() noexcept
{
    return *impl_->sound;
}

struct BmdSink::Impl {
    QString mode_key;
    bool alpha = false;
};

BmdSink::BmdSink(QObject* parent) : OffscreenSink(parent), d_(std::make_unique<Impl>()) {}

BmdSink::~BmdSink()
{
    stop();
}

void BmdSink::set_mode_key(QString key)
{
    d_->mode_key = std::move(key);
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
    return false;
}

OffscreenSink::Status BmdSink::status() const
{
    return {};
}

void BmdSink::set_audio(audio::Mixer* mixer, std::string output_id, bool on)
{
    // Kept, so the output's settings read back; never joined, as no card
    // takes the sound.
    link_.set(mixer, std::move(output_id), on);
}

void BmdSink::push(const float* frames, std::size_t count)
{
    Q_UNUSED(frames);
    Q_UNUSED(count);
}

bool BmdSink::on_start(QSize size)
{
    Q_UNUSED(size);
    set_last_error(bmd_missing_message());
    return false;
}

void BmdSink::on_stop() {}

void BmdSink::deliver(const QImage& image, bool changed)
{
    Q_UNUSED(image);
    Q_UNUSED(changed);
}

bool BmdSink::on_resize(QSize size)
{
    Q_UNUSED(size);
    return false;
}

QSize BmdSink::negotiate_size(QSize want) const
{
    return OffscreenSink::negotiate_size(want);
}

QColor BmdSink::letterbox_color() const
{
    return d_->alpha ? QColor(Qt::transparent) : QColor(Qt::black);
}

int BmdSink::tick_interval_ms() const
{
    return OffscreenSink::tick_interval_ms();
}

BmdDisplayModeInfo BmdSink::active_mode() const
{
    return {};
}

} // namespace seventhslide::render
