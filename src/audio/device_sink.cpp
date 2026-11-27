#include "seventhslide/audio/device_sink.hpp"

#include "ma_devices.hpp"

#include <atomic>

namespace seventhslide::audio {

std::vector<DeviceInfo> playback_devices(AudioEngine::Backend backend)
{
    ma_context context;
    if (!ma::init_context(context, backend)) {
        return {};
    }
    auto out = ma::playback_devices(context);
    ma_context_uninit(&context);
    return out;
}

struct DeviceSink::Impl {
    explicit Impl(AudioEngine::Backend b)
        : backend(b)
    {
    }

    ~Impl()
    {
        close();
        if (context_ok) {
            ma_context_uninit(&context);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    static void on_device_data(ma_device* device, void* output, const void* /*input*/,
                               ma_uint32 frames)
    {
        auto* self = static_cast<DeviceSink*>(device->pUserData);
        self->drift_.pull(static_cast<float*>(output), frames);
        self->impl_->played += frames;
    }

    bool open(DeviceSink& owner, std::string_view device_id)
    {
        close();
        if (!context_tried) {
            context_tried = true;
            context_ok = ma::init_context(context, backend);
        }
        ma_device_id chosen{};
        if (!context_ok || !ma::find_playback_device(context, device_id, chosen)) {
            return false;
        }
        ma_device_config config = ma::playback_config(context);
        config.playback.pDeviceID = &chosen;
        config.dataCallback = &Impl::on_device_data;
        config.pUserData = &owner;
        if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
            return false;
        }
        if (ma_device_start(&device) != MA_SUCCESS) {
            ma_device_uninit(&device);
            return false;
        }
        device_ok = true;
        name = device.playback.name;
        played = 0;
        return true;
    }

    void close()
    {
        if (device_ok) {
            ma_device_uninit(&device);
            device_ok = false;
        }
        name.clear();
    }

    const AudioEngine::Backend backend;
    bool context_tried = false;
    bool context_ok = false;
    ma_context context{};
    ma_device device{};
    bool device_ok = false;
    std::string name;
    std::atomic<std::uint64_t> played{0};
};

DeviceSink::DeviceSink(AudioEngine::Backend backend)
    : impl_(std::make_unique<Impl>(backend))
{
}

DeviceSink::~DeviceSink()
{
    stop();
}

bool DeviceSink::start(Mixer& mixer, std::string output_id, std::string_view device_id)
{
    stop();
    if (!impl_->open(*this, device_id)) {
        return false;
    }
    link_.set(&mixer, std::move(output_id), true);
    link_.join();
    return true;
}

void DeviceSink::stop()
{
    // Nothing pushes once the link has left; then the device stops pulling.
    link_.leave();
    impl_->close();
    drift_.reset();
}

bool DeviceSink::running() const noexcept
{
    return impl_->device_ok;
}

std::string DeviceSink::device_name() const
{
    return impl_->name;
}

void DeviceSink::push(const float* frames, std::size_t count)
{
    drift_.push(frames, count);
}

std::uint64_t DeviceSink::frames_played() const noexcept
{
    return impl_->played.load();
}

} // namespace seventhslide::audio
