#pragma once

// A fixed ring of interleaved stereo frames. Not thread-safe: its owner guards
// it (the players and drift buffers hold it under a short lock). Allocates
// only when made.
// Seam: plain C++20 (no Qt).

#include "seventhslide/audio/audio_format.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace seventhslide::audio {

class FrameRing {
public:
    explicit FrameRing(std::size_t capacity_frames)
        : data_(capacity_frames * k_channels)
        , capacity_(capacity_frames)
    {
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t size() const noexcept
    {
        return static_cast<std::size_t>(written_ - read_);
    }
    [[nodiscard]] std::size_t space() const noexcept { return capacity_ - size(); }
    [[nodiscard]] bool empty() const noexcept { return written_ == read_; }

    /// Frames ever written and read. A loop or a seek can be marked by where
    /// in these counts it falls.
    [[nodiscard]] std::uint64_t written() const noexcept { return written_; }
    [[nodiscard]] std::uint64_t read_count() const noexcept { return read_; }

    /// Append up to ``frames`` frames; returns how many fit.
    std::size_t write(const float* in, std::size_t frames) noexcept
    {
        const std::size_t n = std::min(frames, space());
        for (std::size_t done = 0; done < n;) {
            const std::size_t at = static_cast<std::size_t>(written_ % capacity_);
            const std::size_t run = std::min(n - done, capacity_ - at);
            std::copy_n(in + done * k_channels, run * k_channels, data_.begin() + at * k_channels);
            done += run;
            written_ += run;
        }
        return n;
    }

    /// Copy up to ``frames`` frames from ``offset`` frames past the oldest,
    /// leaving them in the ring; returns how many were there.
    std::size_t peek(float* out, std::size_t frames, std::size_t offset = 0) const noexcept
    {
        if (offset >= size()) {
            return 0;
        }
        const std::size_t n = std::min(frames, size() - offset);
        std::uint64_t from = read_ + offset;
        for (std::size_t done = 0; done < n;) {
            const std::size_t at = static_cast<std::size_t>(from % capacity_);
            const std::size_t run = std::min(n - done, capacity_ - at);
            std::copy_n(data_.begin() + at * k_channels, run * k_channels, out + done * k_channels);
            done += run;
            from += run;
        }
        return n;
    }

    /// Replace up to ``frames`` of the oldest frames with ``in``; returns how
    /// many there were to replace.
    std::size_t overwrite(const float* in, std::size_t frames) noexcept
    {
        const std::size_t n = std::min(frames, size());
        std::uint64_t to = read_;
        for (std::size_t done = 0; done < n;) {
            const std::size_t at = static_cast<std::size_t>(to % capacity_);
            const std::size_t run = std::min(n - done, capacity_ - at);
            std::copy_n(in + done * k_channels, run * k_channels, data_.begin() + at * k_channels);
            done += run;
            to += run;
        }
        return n;
    }

    /// Drop up to ``frames`` of the oldest frames; returns how many went.
    std::size_t discard(std::size_t frames) noexcept
    {
        const std::size_t n = std::min(frames, size());
        read_ += n;
        return n;
    }

    /// Take up to ``frames`` frames; returns how many there were.
    std::size_t read(float* out, std::size_t frames) noexcept
    {
        return discard(peek(out, frames));
    }

    void clear() noexcept { read_ = written_; }

private:
    std::vector<float> data_;
    std::size_t capacity_;
    std::uint64_t written_ = 0;
    std::uint64_t read_ = 0;
};

} // namespace seventhslide::audio
