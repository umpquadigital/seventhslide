#pragma once

// The one format sound has inside SeventhSlide: every file is decoded to it,
// every strip is mixed in it, and every output's mix leaves in it.
// Seam: plain C++20 (no Qt).

#include <cstddef>

namespace seventhslide::audio {

inline constexpr int k_sample_rate = 48000;
/// Stereo, interleaved left then right; samples are 32-bit float, full scale
/// at +-1.
inline constexpr int k_channels = 2;
/// The mixer works a block at a time: 10 ms. Level, mute and Listen changes
/// ramp across one block.
inline constexpr std::size_t k_block_frames = 480;

} // namespace seventhslide::audio
