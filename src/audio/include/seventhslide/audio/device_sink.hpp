#pragma once

// An output's sound on a sound device of its own: a projector's or TV's
// HDMI audio, a USB interface feeding the room's desk. The mixer pushes the
// output's mix on its clock; the device pulls on its own through a
// DriftBuffer, so the two never drift apart.
//
// A device that is not there stays silent; it never falls back to the
// system's default, as Listen plays on: an unplugged projector's sound must
// not come out of the operator's speakers.
//
// Seam: plain C++20 (no Qt); miniaudio stays in the .cpp.

#include "seventhslide/audio/audio_engine.hpp"
#include "seventhslide/audio/drift_buffer.hpp"
#include "seventhslide/audio/mixer.hpp"
#include "seventhslide/audio/mixer_link.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::audio {

/// A playback device.
struct DeviceInfo {
    /// What a setting stores to find it again (the system's name for it).
    std::string id;
    /// What people see.
    std::string name;
    bool is_default = false;
};

/// The sound devices an output can play on.
[[nodiscard]] std::vector<DeviceInfo> playback_devices(
    AudioEngine::Backend backend = AudioEngine::Backend::System);

class DeviceSink final : public OutputAudioSink {
public:
    explicit DeviceSink(AudioEngine::Backend backend = AudioEngine::Backend::System);
    ~DeviceSink() override;

    DeviceSink(const DeviceSink&) = delete;
    DeviceSink& operator=(const DeviceSink&) = delete;

    /// Play output ``output_id``'s mix from ``mixer`` on device
    /// ``device_id`` (a ``playback_devices`` id). Starting again moves it.
    /// False, and silent, when the device is not there or will not open.
    bool start(Mixer& mixer, std::string output_id, std::string_view device_id);
    /// Leave the mixer, then close the device.
    void stop();
    [[nodiscard]] bool running() const noexcept;
    /// The device playing; empty when stopped.
    [[nodiscard]] std::string device_name() const;

    /// The mixer's clock thread.
    void push(const float* frames, std::size_t count) override;

    /// Frames the device has taken since it started (tests).
    [[nodiscard]] std::uint64_t frames_played() const noexcept;
    [[nodiscard]] const DriftBuffer& buffer() const noexcept { return drift_; }

private:
    struct Impl;
    DriftBuffer drift_;
    std::unique_ptr<Impl> impl_;
    /// Last: it leaves the mixer before the rest goes.
    MixerLink link_{*this};
};

} // namespace seventhslide::audio
