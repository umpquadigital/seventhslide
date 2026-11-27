#include <seventhslide/render/bmd_input.hpp>

#include "bmd_internal.hpp"

#include <seventhslide/audio/audio_format.hpp>
#include <seventhslide/render/deinterlace.hpp>

#include <QCoreApplication>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>

namespace seventhslide::render {

std::vector<BmdDeviceInfo> bmd_list_input_devices()
{
    std::vector<BmdDeviceInfo> out;
    if (!bmd_runtime_available()) {
        return out;
    }
    bmd::ComRef<IDeckLinkIterator> it(CreateDeckLinkIteratorInstance());
    if (!it) {
        return out;
    }
    IDeckLink* raw = nullptr;
    while (it->Next(&raw) == S_OK && raw != nullptr) {
        bmd::ComRef<IDeckLink> device(raw);
        raw = nullptr;
        BmdDeviceInfo info = bmd::describe(device.get());
        if (info.can_capture) {
            out.push_back(std::move(info));
        }
    }
    return out;
}

/// The input and the callback the driver calls with its frames.
struct BmdReceiver::Impl final : public IDeckLinkInputCallback {
    QString name;
    FrameReady on_frame;
    audio::StreamSource own_sound;
    audio::StreamSource* sound = &own_sound;
    bmd::ComRef<IDeckLinkInput> input;
    bool capturing = false;
    /// The signal is in fields (1080i, 576i): its pictures are made whole.
    /// The starting mode is.
    std::atomic<bool> interlaced{true};
    std::atomic<ULONG> refs{1};
    mutable std::mutex mu;
    QImage frame;
    Stats stats;
    QString problem;
    std::vector<float> samples;

    // IUnknown: the driver holds a reference while the callback is set.
    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override
    {
        const auto is = [&iid](REFIID other) { return std::memcmp(&iid, &other, sizeof(REFIID)) == 0; };
        if (is(bmd::iid_unknown()) || is(IID_IDeckLinkInputCallback)) {
            *ppv = static_cast<IDeckLinkInputCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++refs; }
    ULONG Release() override { return --refs; }  // owned by BmdReceiver, never deleted here

    HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events,
                                    IDeckLinkDisplayMode* mode,
                                    BMDDetectedVideoInputFormatFlags detected) override
    {
        (void)events;  // any change - the mode, or RGB against YUV - is followed
        if (mode == nullptr) {
            return S_OK;
        }
        // Follow the signal: the mode it is in, and RGB if it is sent as RGB.
        const BMDPixelFormat pixels = (detected & bmdDetectedVideoInputRGB444) != 0
                                          ? bmdFormat8BitBGRA
                                          : bmdFormat8BitYUV;
        const BMDFieldDominance fields = mode->GetFieldDominance();
        interlaced = fields == bmdLowerFieldFirst || fields == bmdUpperFieldFirst;
        input->PauseStreams();
        input->EnableVideoInput(mode->GetDisplayMode(), pixels, bmdVideoInputEnableFormatDetection);
        input->FlushStreams();
        input->StartStreams();
        BMDTimeValue duration = 0;
        BMDTimeScale scale = 0;
        if (mode->GetFrameRate(&duration, &scale) == S_OK && duration > 0) {
            std::lock_guard lock(mu);
            stats.frame_rate = double(scale) / double(duration);
        }
        return S_OK;
    }

    HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame* video,
                                   IDeckLinkAudioInputPacket* audio_packet) override
    {
        if (video != nullptr) {
            take_video(video);
        }
        if (audio_packet != nullptr) {
            take_audio(audio_packet);
        }
        return S_OK;
    }

    void take_video(IDeckLinkVideoInputFrame* video)
    {
        if ((video->GetFlags() & bmdFrameHasNoInputSource) != 0) {
            std::lock_guard lock(mu);
            problem = QCoreApplication::translate("BmdReceiver", "No signal on “%1”.").arg(name);
            return;
        }
        void* bytes = nullptr;
        if (video->GetBytes(&bytes) != S_OK || bytes == nullptr) {
            return;
        }
        const int w = int(video->GetWidth());
        const int h = int(video->GetHeight());
        const int row = int(video->GetRowBytes());
        QImage picture;
        if (video->GetPixelFormat() == bmdFormat8BitBGRA) {
            picture = QImage(static_cast<const uchar*>(bytes), w, h, row, QImage::Format_RGB32).copy();
        } else {
            picture = bmd_uyvy_to_image(static_cast<const std::uint8_t*>(bytes), w, h, row);
        }
        if (interlaced) {
            deinterlace(picture);
        }
        {
            std::lock_guard lock(mu);
            frame = std::move(picture);
            stats.size = QSize(w, h);
            ++stats.frames;
            problem.clear();
        }
        if (on_frame) {
            on_frame();
        }
    }

    void take_audio(IDeckLinkAudioInputPacket* packet)
    {
        void* bytes = nullptr;
        const long frames = packet->GetSampleFrameCount();
        if (frames <= 0 || packet->GetBytes(&bytes) != S_OK || bytes == nullptr) {
            return;
        }
        // 32-bit integer, two channels, interleaved: as asked for.
        const auto* pcm = static_cast<const std::int32_t*>(bytes);
        const auto n = static_cast<std::size_t>(frames) * audio::k_channels;
        samples.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            samples[i] = static_cast<float>(pcm[i] / 2147483648.0);
        }
        sound->push(samples.data(), static_cast<std::size_t>(frames), audio::k_sample_rate);
    }
};

BmdReceiver::BmdReceiver(QString device_name, FrameReady on_frame, audio::StreamSource* sound)
    : impl_(std::make_unique<Impl>())
{
    impl_->name = std::move(device_name);
    impl_->on_frame = std::move(on_frame);
    if (sound != nullptr) {
        impl_->sound = sound;
    }
    if (!bmd_runtime_available()) {
        impl_->problem = bmd_missing_message();
        return;
    }
    // By name, as the Live tab lists it.
    bmd::ComRef<IDeckLinkIterator> it(CreateDeckLinkIteratorInstance());
    IDeckLink* raw = nullptr;
    bmd::ComRef<IDeckLink> device;
    while (it && it->Next(&raw) == S_OK && raw != nullptr) {
        bmd::ComRef<IDeckLink> candidate(raw);
        raw = nullptr;
        const auto info = bmd::describe(candidate.get());
        if (info.can_capture && info.name == impl_->name) {
            device = std::move(candidate);
            break;
        }
    }
    if (!device) {
        impl_->problem = QCoreApplication::translate("BmdReceiver",
                                                     "Waiting for “%1” to be connected.")
                             .arg(impl_->name);
        return;
    }
    if (device->QueryInterface(IID_IDeckLinkInput, reinterpret_cast<void**>(impl_->input.receive()))
            != S_OK
        || !impl_->input) {
        impl_->problem = QCoreApplication::translate("BmdReceiver", "“%1” has no input.")
                             .arg(impl_->name);
        return;
    }
    // A starting mode, until the signal says what it is.
    impl_->input->SetCallback(impl_.get());
    if (impl_->input->EnableVideoInput(bmdModeHD1080i5994, bmdFormat8BitYUV,
                                       bmdVideoInputEnableFormatDetection)
            != S_OK
        || impl_->input->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType32bitInteger,
                                          audio::k_channels)
               != S_OK
        || impl_->input->StartStreams() != S_OK) {
        impl_->input->SetCallback(nullptr);
        impl_->problem = QCoreApplication::translate(
                             "BmdReceiver", "“%1” is in use by another program.")
                             .arg(impl_->name);
        impl_->input.reset();
        return;
    }
    impl_->capturing = true;
    impl_->problem = QCoreApplication::translate("BmdReceiver", "Waiting for a signal on “%1”.")
                         .arg(impl_->name);
}

BmdReceiver::~BmdReceiver()
{
    if (impl_->input) {
        impl_->input->StopStreams();
        impl_->input->DisableAudioInput();
        impl_->input->DisableVideoInput();
        impl_->input->SetCallback(nullptr);
    }
}

bool BmdReceiver::available() const noexcept
{
    return impl_->capturing;
}

QString BmdReceiver::problem() const
{
    std::lock_guard lock(impl_->mu);
    return impl_->problem;
}

QImage BmdReceiver::frame() const
{
    std::lock_guard lock(impl_->mu);
    return impl_->frame;
}

BmdReceiver::Stats BmdReceiver::stats() const
{
    std::lock_guard lock(impl_->mu);
    return impl_->stats;
}

audio::StreamSource& BmdReceiver::sound() noexcept
{
    return *impl_->sound;
}

} // namespace seventhslide::render
