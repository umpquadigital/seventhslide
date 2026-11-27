#include "ma_devices.hpp"

namespace seventhslide::audio::ma {
namespace {

/// PulseAudio (and PipeWire's stand-in for it) takes miniaudio's period as
/// the stream's whole buffer and runs the sound server at about a quarter of
/// it. A 10 ms block there held the whole system at 128 frames (2.7 ms), and
/// the device dropped out, crackling, whenever this or any other program was
/// a little late. 2048 asks for a 32 ms buffer, which the server serves 512
/// frames (10.7 ms) at a time.
constexpr ma_uint32 k_pulse_period_frames = 2048;

} // namespace

bool init_context(ma_context& context, AudioEngine::Backend backend)
{
    const ma_backend null_only[] = {ma_backend_null};
    const bool null = backend == AudioEngine::Backend::Null;
    return ma_context_init(null ? null_only : nullptr, null ? 1 : 0, nullptr, &context)
           == MA_SUCCESS;
}

std::string device_key(ma_backend backend, const ma_device_info& info)
{
    switch (backend) {
    case ma_backend_pulseaudio:
        return info.id.pulse;
    case ma_backend_alsa:
        return info.id.alsa;
    case ma_backend_coreaudio:
        return info.id.coreaudio;
    default:
        return info.name;
    }
}

std::vector<DeviceInfo> playback_devices(ma_context& context)
{
    std::vector<DeviceInfo> out;
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
        return out;
    }
    for (ma_uint32 i = 0; i < count; ++i) {
        out.push_back({device_key(context.backend, infos[i]), infos[i].name,
                       infos[i].isDefault != 0});
    }
    return out;
}

bool find_playback_device(ma_context& context, std::string_view key, ma_device_id& out)
{
    if (key.empty()) {
        return false;
    }
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
        return false;
    }
    for (ma_uint32 i = 0; i < count; ++i) {
        if (device_key(context.backend, infos[i]) == key) {
            out = infos[i].id;
            return true;
        }
    }
    return false;
}

ma_device_config playback_config(const ma_context& context)
{
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = k_channels;
    config.sampleRate = k_sample_rate;
    if (context.backend == ma_backend_pulseaudio) {
        config.periodSizeInFrames = k_pulse_period_frames;
        // Called for what the server asks, not a whole period at a time: a
        // period's worth at once would reach the mixer's sinks as 43 ms
        // bursts.
        config.noFixedSizedCallback = MA_TRUE;
    } else {
        config.periodSizeInFrames = static_cast<ma_uint32>(k_block_frames);
    }
    return config;
}

} // namespace seventhslide::audio::ma
