#pragma once

// The clock the Audio Mixer runs on and the device Listen plays on.
//
// Every 10 ms the engine has the mixer make one block: each output's mix goes
// to its sinks, and the Listen mix to this computer's headphones or speakers.
// The Listen device's own clock drives it, so the device runs even when
// nothing is on Listen (it plays silence) and the outputs keep their sound.
// With no device that will open, a timer keeps the same time instead; and
// should the device stop (unplugged, its sound server gone) the timer takes
// over within 100 ms (three periods, for a device that asks seldom), so the
// outputs' sound never stops with the headphones.
//
// Seam: plain C++20 (no Qt); miniaudio stays in the .cpp.

#include "seventhslide/audio/mixer.hpp"

#include <cstddef>
#include <memory>
#include <string>

namespace seventhslide::audio {

class AudioEngine {
public:
    enum class Backend {
        /// The system's sound (PulseAudio / PipeWire, ALSA, ... on Linux).
        System,
        /// miniaudio's null device: real time, no sound. For tests.
        Null,
    };

    /// Nothing runs and no sound system is opened until ``start`` or
    /// ``start_timer``.
    explicit AudioEngine(Backend backend = Backend::System);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    [[nodiscard]] Mixer& mixer() noexcept { return mixer_; }

    /// Start the clock on the system's default device, or on the timer when
    /// none opens. True when a device plays; false on the timer.
    bool start();
    /// Start the clock on the timer, with no device: the outputs get their
    /// sound and Listen is heard nowhere. What the app runs on until it asks
    /// for a device, and what tests run on.
    void start_timer();
    void stop();
    [[nodiscard]] bool running() const noexcept;
    /// The device playing, or empty on the timer.
    [[nodiscard]] std::string device_name() const;

    /// Stop the device as the system would when it is unplugged, leaving the
    /// engine started. Tests only.
    void stop_device_for_tests();

    /// Without a clock: mix ``frames`` frames into ``listen`` as the device
    /// would. For tests and tools; not while started.
    void render(float* listen, std::size_t frames);

private:
    struct Impl;
    Mixer mixer_;
    std::unique_ptr<Impl> impl_;
};

} // namespace seventhslide::audio
