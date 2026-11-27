#include "seventhslide/audio/drift_buffer.hpp"

#include <algorithm>
#include <cmath>

namespace seventhslide::audio {
namespace {

/// The longest pull handled in one piece; longer ones are split.
constexpr std::size_t k_max_chunk = 4096;

// The correction is a PI controller on how far the fill is from the target
// (as a fraction of it). Proportional: a buffer 30% over reads 0.3% faster.
// Integral (per second): settles a steady drift in about half a minute,
// with the fill back at the target. Both are slow next to a block, so a
// correction is never heard as a pitch change.
constexpr double k_proportional = 0.01;
constexpr double k_integral = 0.002;
/// How quickly the measured fill is smoothed: a time constant of 0.5 s,
/// longer than the jitter of blocks arriving.
constexpr double k_fill_smoothing_sec = 0.5;

/// A smoothed fill this many times what it aims for is more than a drift (a
/// sink that stalled), too much to read away at 0.5%: it is dropped at once.
constexpr double k_skip_over = 3.0;
/// The cross-fade across frames dropped: 10 ms.
constexpr std::size_t k_skip_fade = k_block_frames;
static_assert(2 * k_skip_fade <= k_max_chunk, "the fade is made in window_");

/// Catmull-Rom between ``x0`` and ``x1`` at ``t`` (0 to 1).
float interpolate(float xm1, float x0, float x1, float x2, float t)
{
    const float c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}

} // namespace

DriftBuffer::DriftBuffer(std::size_t target_frames)
    : ring_(std::max<std::size_t>(target_frames, k_block_frames) * 4 + k_max_chunk * 2)
    , target_(std::max<std::size_t>(target_frames, k_block_frames))
    , window_((static_cast<std::size_t>(k_max_chunk * (1.0 + k_max_correction)) + 8) * k_channels)
{
    average_fill_ = static_cast<double>(target_);
}

void DriftBuffer::push(const float* frames, std::size_t count)
{
    std::lock_guard lock(mu_);
    if (count > ring_.space()) {
        ring_.discard(count - ring_.space());
        ++overruns_;
    }
    ring_.write(frames, count);
}

void DriftBuffer::pull(float* out, std::size_t count)
{
    std::lock_guard lock(mu_);
    while (count > 0) {
        const std::size_t n = std::min(count, k_max_chunk);
        pull_chunk(out, n);
        out += n * k_channels;
        count -= n;
    }
}

void DriftBuffer::pull_chunk(float* out, std::size_t count)
{
    const auto silence = [&] { std::fill_n(out, count * k_channels, 0.0f); };
    // What to hold before a pull: the target, and at least twice the pull. A
    // sink that pulls about a target's worth at a time (DeckLink at 25p, a
    // PipeWire graph of 2048 frames) would otherwise settle where its pulls
    // run dry, and while the controller catches up with a drift the fill sits
    // below what it aims for. Starting, this pull's own size decides.
    if (!primed_) {
        if (ring_.size() < std::max(target_, 2 * count)) {
            silence();
            return;
        }
        primed_ = true;
        phase_ = 0.0;
        previous_.fill(0.0f);
        average_fill_ = static_cast<double>(ring_.size());
    }

    // Steer by the smoothed fill, for twice the smoothed pull: a sink whose
    // pulls vary (DeckLink tops its card up by what it played) has a steady
    // aim.
    const double dt = static_cast<double>(count) / k_sample_rate;
    const double alpha = std::min(1.0, dt / k_fill_smoothing_sec);
    average_fill_ += (static_cast<double>(ring_.size()) - average_fill_) * alpha;
    average_pull_ = average_pull_ > 0.0
                        ? average_pull_ + (static_cast<double>(count) - average_pull_) * alpha
                        : static_cast<double>(count);
    const double setpoint = std::max(static_cast<double>(target_), 2.0 * average_pull_);
    // Never less than this pull reads, with as much again in hand: a sink
    // that suddenly pulls far more than it has been would otherwise run dry.
    const auto keep =
        static_cast<std::size_t>(std::max(setpoint, 2.0 * static_cast<double>(count)));
    if (average_fill_ > k_skip_over * setpoint && ring_.size() > keep + k_skip_fade) {
        skip_to(keep);
        average_fill_ = static_cast<double>(ring_.size());
    }
    const double error = (average_fill_ - setpoint) / setpoint;
    const double integral_limit = k_max_correction / k_integral;
    integral_ = std::clamp(integral_ + error * dt, -integral_limit, integral_limit);
    ratio_ = 1.0 + std::clamp(k_proportional * error + k_integral * integral_,
                              -k_max_correction, k_max_correction);

    // Frames from the ring this pull reads, with one before (previous_) and
    // two after the last position for the interpolation.
    const double last = phase_ + static_cast<double>(count - 1) * ratio_;
    const auto needed = static_cast<std::size_t>(std::floor(last)) + 3;
    if (ring_.size() < needed) {
        ++underruns_;
        primed_ = false;
        silence();
        return;
    }
    std::copy(previous_.begin(), previous_.end(), window_.begin());
    ring_.peek(window_.data() + k_channels, needed);
    const float* x = window_.data() + k_channels;  // x[-1] is previous_

    for (std::size_t f = 0; f < count; ++f) {
        const double p = phase_ + static_cast<double>(f) * ratio_;
        const auto i = static_cast<std::ptrdiff_t>(std::floor(p));
        const auto t = static_cast<float>(p - static_cast<double>(i));
        for (int c = 0; c < k_channels; ++c) {
            const auto at = [&](std::ptrdiff_t k) { return x[k * k_channels + c]; };
            out[f * k_channels + c] =
                t == 0.0f ? at(i) : interpolate(at(i - 1), at(i), at(i + 1), at(i + 2), t);
        }
    }

    const double end = phase_ + static_cast<double>(count) * ratio_;
    const auto consumed = static_cast<std::size_t>(std::floor(end));
    if (consumed > 0) {
        std::copy_n(x + (consumed - 1) * k_channels, k_channels, previous_.begin());
        ring_.discard(consumed);
    }
    phase_ = end - static_cast<double>(consumed);
}

void DriftBuffer::skip_to(std::size_t keep)
{
    // The fade runs from the frames next due (so it follows on from previous_)
    // into the first of those kept, which then stand in their place.
    const std::size_t drop = ring_.size() - keep;
    const float* from = window_.data();
    float* to = window_.data() + k_skip_fade * k_channels;
    ring_.peek(window_.data(), k_skip_fade);
    ring_.peek(to, k_skip_fade, drop);
    for (std::size_t f = 0; f < k_skip_fade; ++f) {
        const float w = static_cast<float>(f + 1) / static_cast<float>(k_skip_fade + 1);
        for (int c = 0; c < k_channels; ++c) {
            const std::size_t i = f * k_channels + c;
            to[i] = from[i] + (to[i] - from[i]) * w;
        }
    }
    ring_.discard(drop);
    ring_.overwrite(to, k_skip_fade);
}

double DriftBuffer::ratio() const
{
    std::lock_guard lock(mu_);
    return ratio_;
}

std::size_t DriftBuffer::fill() const
{
    std::lock_guard lock(mu_);
    return ring_.size();
}

std::uint64_t DriftBuffer::underruns() const
{
    std::lock_guard lock(mu_);
    return underruns_;
}

std::uint64_t DriftBuffer::overruns() const
{
    std::lock_guard lock(mu_);
    return overruns_;
}

void DriftBuffer::reset()
{
    std::lock_guard lock(mu_);
    reset_locked();
}

void DriftBuffer::reset_locked()
{
    ring_.clear();
    primed_ = false;
    phase_ = 0.0;
    previous_.fill(0.0f);
    ratio_ = 1.0;
    integral_ = 0.0;
    average_fill_ = static_cast<double>(target_);
    average_pull_ = 0.0;
    underruns_ = 0;
    overruns_ = 0;
}

} // namespace seventhslide::audio
