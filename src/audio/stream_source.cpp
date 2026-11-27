#include "seventhslide/audio/stream_source.hpp"

#include <algorithm>
#include <cmath>

namespace seventhslide::audio {

StreamSource::StreamSource(std::size_t target_frames)
    : drift_(target_frames)
{
}

void StreamSource::push(const float* frames, std::size_t count, int rate)
{
    if (frames == nullptr || count == 0 || rate < k_min_rate || rate > k_max_rate) {
        return;
    }
    std::lock_guard lock(mu_);
    if (rate != rate_) {
        rate_ = rate;
        position_ = 0.0;
        has_previous_ = false;
    }
    if (rate == k_sample_rate) {
        drift_.push(frames, count);
        return;
    }
    // Input frame ``i``, or the one before this chunk for ``i`` = -1.
    const auto sample = [&](long long i, std::size_t channel) {
        return i < 0 ? previous_[channel] : frames[static_cast<std::size_t>(i) * k_channels + channel];
    };
    if (!has_previous_ && position_ < 0.0) {
        position_ = 0.0;
    }
    const double step = static_cast<double>(rate) / static_cast<double>(k_sample_rate);
    resampled_.clear();
    while (true) {
        const auto at = static_cast<long long>(std::floor(position_));
        if (at + 1 >= static_cast<long long>(count)) {
            break;
        }
        const auto t = static_cast<float>(position_ - static_cast<double>(at));
        for (std::size_t c = 0; c < k_channels; ++c) {
            const float a = sample(at, c);
            const float b = sample(at + 1, c);
            resampled_.push_back(a + (b - a) * t);
        }
        position_ += step;
    }
    position_ -= static_cast<double>(count);
    for (std::size_t c = 0; c < k_channels; ++c) {
        previous_[c] = frames[(count - 1) * k_channels + c];
    }
    has_previous_ = true;
    if (!resampled_.empty()) {
        drift_.push(resampled_.data(), resampled_.size() / k_channels);
    }
}

void StreamSource::reset()
{
    std::lock_guard lock(mu_);
    drift_.reset();
    position_ = 0.0;
    has_previous_ = false;
}

void StreamSource::read(float* out, std::size_t frames)
{
    drift_.pull(out, frames);
}

void StreamMix::push(std::int64_t stream, const float* frames, std::size_t count, int rate)
{
    StreamSource* source = nullptr;
    // Streams that ended are freed after the lock, on this thread, never on
    // the audio thread.
    std::array<std::unique_ptr<StreamSource>, k_max_streams> let_go;
    {
        std::lock_guard lock(mu_);
        std::size_t gone = 0;
        for (auto it = streams_.begin(); it != streams_.end();) {
            if (ended(*it)) {
                let_go[gone++] = std::move(it->source);
                it = streams_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& s : streams_) {
            if (s.id == stream) {
                source = s.source.get();
                s.last_push = frames_read_;
            }
        }
        if (source == nullptr) {
            if (streams_.size() >= k_max_streams) {
                return;
            }
            // Made here, on the sender's side, never on the audio thread.
            streams_.push_back({stream, std::make_unique<StreamSource>(), frames_read_});
            source = streams_.back().source.get();
        }
    }
    // Only the sender removes streams, so ``source`` outlives the lock.
    source->push(frames, count, rate);
}

void StreamMix::read(float* out, std::size_t frames)
{
    std::fill_n(out, frames * k_channels, 0.0f);
    std::lock_guard lock(mu_);
    for (auto& s : streams_) {
        if (ended(s)) {
            continue;
        }
        for (std::size_t done = 0; done < frames;) {
            const std::size_t n = std::min(frames - done, k_block_frames);
            s.source->read(scratch_.data(), n);
            float* to = out + done * k_channels;
            for (std::size_t i = 0; i < n * k_channels; ++i) {
                to[i] += scratch_[i];
            }
            done += n;
        }
    }
    frames_read_ += frames;
}

std::size_t StreamMix::stream_count() const
{
    std::lock_guard lock(mu_);
    return streams_.size();
}

} // namespace seventhslide::audio

