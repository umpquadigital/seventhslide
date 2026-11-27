#include "seventhslide/audio/audio_engine.hpp"

#include "ma_devices.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

namespace seventhslide::audio {
namespace {

using Clock = std::chrono::steady_clock;

/// A device whose callback has been quiet this long has stopped (unplugged,
/// its sound server gone): the timer keeps the mixer's time until it is back.
/// At least three of the device's own periods, for a device that asks
/// seldom (Bluetooth, a large ALSA buffer).
constexpr auto k_device_stall = std::chrono::milliseconds(100);
constexpr int k_stall_periods = 3;

std::int64_t now_ns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
        .count();
}

} // namespace

struct AudioEngine::Impl {
    Impl(Mixer& m, Backend b)
        : mixer(m)
        , backend(b)
    {
    }

    /// The sound system, opened the first time a device is wanted.
    bool ensure_context()
    {
        if (!context_tried) {
            context_tried = true;
            context_ok = ma::init_context(context, backend);
        }
        return context_ok;
    }

    ~Impl()
    {
        stop();
        if (context_ok) {
            ma_context_uninit(&context);
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    /// The device asks for ``frames``; the mixer makes whole blocks.
    void pull(float* out, std::size_t frames)
    {
        last_device_pull_ns = now_ns();
        while (frames > 0) {
            if (block_left == 0) {
                mixer.process(block.data(), k_block_frames);
                block_left = k_block_frames;
            }
            const std::size_t n = std::min(frames, block_left);
            const std::size_t from = (k_block_frames - block_left) * k_channels;
            std::copy_n(block.data() + from, n * k_channels, out);
            out += n * k_channels;
            frames -= n;
            block_left -= n;
        }
    }

    static void on_device_data(ma_device* device, void* output, const void* /*input*/,
                               ma_uint32 frames)
    {
        static_cast<Impl*>(device->pUserData)->pull(static_cast<float*>(output), frames);
    }

    bool start_device()
    {
        if (!ensure_context()) {
            return false;
        }
        ma_device_config config = ma::playback_config(context);
        config.dataCallback = &Impl::on_device_data;
        config.pUserData = this;
        block_left = 0;
        if (ma_device_init(&context, &config, &device) != MA_SUCCESS) {
            return false;
        }
        // Counted as live from now, so the timer does not step in while the
        // device opens.
        last_device_pull_ns = now_ns();
        if (ma_device_start(&device) != MA_SUCCESS) {
            ma_device_uninit(&device);
            return false;
        }
        // How quiet the device may be before the timer steps in.
        const auto period_ns = static_cast<std::int64_t>(device.playback.internalPeriodSizeInFrames)
                             * 1'000'000'000
                             / std::max<std::int64_t>(device.playback.internalSampleRate, 1);
        stall_ns = std::max<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(k_device_stall).count(),
            k_stall_periods * period_ns);
        device_ok = true;
        name = device.playback.name;
        return true;
    }

    /// The timer runs whenever the engine does. With no device it is the
    /// clock; beside a device it is a watchdog that keeps time only while
    /// the device's callback has stalled, so the outputs' sound never stops
    /// with the headphones.
    void start_timer()
    {
        timer_stop = false;
        timer = std::thread([this] {
            constexpr auto period = std::chrono::microseconds(
                static_cast<long long>(k_block_frames) * 1'000'000 / k_sample_rate);
            std::array<float, k_block_frames * k_channels> discard{};
            auto next = Clock::now();
            while (!timer_stop) {
                const auto quiet = now_ns() - last_device_pull_ns.load();
                if (!device_ok || quiet > stall_ns.load()) {
                    mixer.process(discard.data(), k_block_frames);
                }
                next += period;
                // Asleep too long (a suspended laptop): carry on from now
                // rather than racing to catch up.
                if (Clock::now() - next > std::chrono::milliseconds(100)) {
                    next = Clock::now();
                }
                std::this_thread::sleep_until(next);
            }
        });
    }

    void stop()
    {
        if (device_ok) {
            ma_device_uninit(&device);
            device_ok = false;
        }
        if (timer.joinable()) {
            timer_stop = true;
            timer.join();
        }
        name.clear();
    }

    Mixer& mixer;
    const Backend backend;
    bool context_tried = false;
    bool context_ok = false;
    ma_context context{};
    /// Read by the timer thread.
    std::atomic<bool> device_ok{false};
    std::atomic<std::int64_t> last_device_pull_ns{0};
    std::atomic<std::int64_t> stall_ns{0};
    ma_device device{};
    std::string name;
    std::thread timer;
    std::atomic<bool> timer_stop{false};
    /// The block the device is part way through. The clock's thread only.
    std::array<float, k_block_frames * k_channels> block{};
    std::size_t block_left = 0;
};

AudioEngine::AudioEngine(Backend backend)
    : impl_(std::make_unique<Impl>(mixer_, backend))
{
}

AudioEngine::~AudioEngine()
{
    impl_->stop();
}

bool AudioEngine::start()
{
    impl_->stop();
    const bool on_device = impl_->start_device();
    impl_->start_timer();
    return on_device;
}

void AudioEngine::start_timer()
{
    impl_->stop();
    impl_->start_timer();
}

void AudioEngine::stop()
{
    impl_->stop();
}

bool AudioEngine::running() const noexcept
{
    return impl_->timer.joinable();
}

void AudioEngine::stop_device_for_tests()
{
    if (impl_->device_ok) {
        ma_device_stop(&impl_->device);
    }
}

std::string AudioEngine::device_name() const
{
    return impl_->name;
}

void AudioEngine::render(float* listen, std::size_t frames)
{
    while (frames > 0) {
        const std::size_t n = std::min(frames, k_block_frames);
        mixer_.process(listen, n);
        listen += n * k_channels;
        frames -= n;
    }
}

} // namespace seventhslide::audio
