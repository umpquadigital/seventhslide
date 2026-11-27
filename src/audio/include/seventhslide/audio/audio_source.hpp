#pragma once

// Something a stack plays into the mixer: an audio item, later a video's
// sound. Seam: plain C++20 (no Qt).

#include <cstddef>

namespace seventhslide::audio {

class AudioSource {
public:
    virtual ~AudioSource() = default;

    /// Write exactly ``frames`` frames (interleaved stereo, ``k_sample_rate``)
    /// to ``out``: what plays next, then silence for whatever is not ready
    /// (paused, still loading, ended). Called on the audio thread once a
    /// block: it must not wait on anything slower than a short lock, and must
    /// not allocate. ``Mixer::remove_source`` calls it once more, on the
    /// caller's thread, for the block that fades out.
    virtual void read(float* out, std::size_t frames) = 0;
};

} // namespace seventhslide::audio
