#pragma once

// Sound handed in by something that keeps its own time (a web page's), as a
// stack's source in the mixer. The sender pushes chunks as they come, at its
// own rate; the mixer pulls a block every 10 ms by the engine's clock. A
// DriftBuffer between them keeps about ``target`` frames in hand and absorbs
// both the chunks' jitter and the two clocks' drift. Sound at another rate
// than the mixer's is resampled (linearly) on the way in.
// Seam: plain C++20 (no Qt).

#include "seventhslide/audio/audio_format.hpp"
#include "seventhslide/audio/audio_source.hpp"
#include "seventhslide/audio/drift_buffer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace seventhslide::audio {

class StreamSource final : public AudioSource {
public:
    /// Rates outside this are not sound a page makes; their chunks are dropped.
    static constexpr int k_min_rate = 8000;
    static constexpr int k_max_rate = 192000;

    /// ``target_frames``: what it keeps in hand; 125 ms by default, enough for
    /// a page's chunks of up to about 85 ms to arrive unevenly.
    explicit StreamSource(std::size_t target_frames = k_sample_rate / 8);

    /// The sender's side, any thread: ``frames`` interleaved stereo frames at
    /// ``rate`` frames a second.
    void push(const float* frames, std::size_t count, int rate);

    /// Back to empty, as new (the sender started again).
    void reset();

    /// The mixer's side (audio thread).
    void read(float* out, std::size_t frames) override;

    [[nodiscard]] const DriftBuffer& buffer() const noexcept { return drift_; }

private:
    DriftBuffer drift_;
    /// The resampler's state, the sender's side only.
    std::mutex mu_;
    int rate_ = k_sample_rate;
    /// Where the next output frame falls, in input frames past the start of
    /// the chunk being read; -1..0 falls between ``previous_`` and it.
    double position_ = 0.0;
    std::array<float, k_channels> previous_{};
    bool has_previous_ = false;
    std::vector<float> resampled_;
};

/// Several streams from one sender, summed: a page's own Web Audio and its
/// video each send their own, each on its own clock and in its own chunks,
/// so each has its own StreamSource. A stream that has sent nothing for
/// ``k_idle_frames`` of the mixer's time has ended (its audio context closed,
/// its page gone): it is no longer mixed, and the next push lets it go.
class StreamMix final : public AudioSource {
public:
    /// More streams than this at once from one page are not heard.
    static constexpr std::size_t k_max_streams = 8;
    /// One second.
    static constexpr std::uint64_t k_idle_frames = k_sample_rate;

    /// The sender's side (one thread): ``count`` frames of stream ``stream``
    /// at ``rate``.
    void push(std::int64_t stream, const float* frames, std::size_t count, int rate);

    /// The mixer's side (audio thread): every stream still sending, summed.
    void read(float* out, std::size_t frames) override;

    /// Streams held, those that ended and are not yet let go among them.
    [[nodiscard]] std::size_t stream_count() const;

private:
    struct Stream {
        std::int64_t id = 0;
        std::unique_ptr<StreamSource> source;
        /// ``frames_read_`` at its last push.
        std::uint64_t last_push = 0;
    };
    /// Whether ``stream`` has sent nothing for ``k_idle_frames``. Under ``mu_``.
    [[nodiscard]] bool ended(const Stream& stream) const noexcept
    {
        return frames_read_ - stream.last_push > k_idle_frames;
    }

    mutable std::mutex mu_;
    std::vector<Stream> streams_;
    /// The mixer's clock: frames read so far.
    std::uint64_t frames_read_ = 0;
    std::array<float, k_block_frames * k_channels> scratch_{};
};

} // namespace seventhslide::audio

