#pragma once

// Carries sound from the engine's clock to a sink's own. The engine pushes a
// block every 10 ms by its device's clock; a sink (a sound card, a DeckLink
// card, NDI paced by the system clock) pulls by another. No two clocks agree
// exactly, so the buffer keeps about ``target`` frames between them (more
// for a sink whose pulls are nearly that large: see ``pull_chunk``) and
// reads slightly faster or slower (at most 0.5%) to hold that, resampling
// smoothly rather than dropping or repeating frames. While the clocks agree
// it passes the sound through untouched. Far over its target, as after the
// sink stalled, it drops back to the target at once, faded across.
// Seam: plain C++20 (no Qt).

#include "seventhslide/audio/audio_format.hpp"
#include "seventhslide/audio/frame_ring.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace seventhslide::audio {

class DriftBuffer {
public:
    /// The largest correction: 0.5% faster or slower.
    static constexpr double k_max_correction = 0.005;

    /// ``target_frames``: what it keeps between the clocks; 40 ms by default.
    explicit DriftBuffer(std::size_t target_frames = k_sample_rate / 25);

    /// The engine's side: append ``count`` frames. When the sink has stopped
    /// pulling and the buffer is full, the oldest frames make room.
    void push(const float* frames, std::size_t count);

    /// The sink's side: exactly ``count`` frames. Silence until the buffer
    /// first holds its target, and again after running dry (counted in
    /// ``underruns``) until it has refilled.
    void pull(float* out, std::size_t count);

    /// Input frames read per output frame: below 1 when the sink's clock runs
    /// fast, above when it runs slow.
    [[nodiscard]] double ratio() const;
    [[nodiscard]] std::size_t fill() const;
    [[nodiscard]] std::size_t target() const noexcept { return target_; }
    [[nodiscard]] std::uint64_t underruns() const;
    [[nodiscard]] std::uint64_t overruns() const;

    /// Back to empty, as new.
    void reset();

private:
    void pull_chunk(float* out, std::size_t count);
    /// Drop all but the newest ``keep`` frames, fading from the oldest into
    /// what is kept.
    void skip_to(std::size_t keep);
    void reset_locked();

    mutable std::mutex mu_;
    FrameRing ring_;
    const std::size_t target_;
    bool primed_ = false;
    /// Where the next output frame falls, in frames past the ring's oldest.
    double phase_ = 0.0;
    /// The frame before the ring's oldest, for interpolation.
    std::array<float, k_channels> previous_{};
    double ratio_ = 1.0;
    double integral_ = 0.0;
    double average_fill_ = 0.0;
    /// The pulls' size, smoothed as the fill is; 0 before the first.
    double average_pull_ = 0.0;
    std::vector<float> window_;
    std::uint64_t underruns_ = 0;
    std::uint64_t overruns_ = 0;
};

} // namespace seventhslide::audio
