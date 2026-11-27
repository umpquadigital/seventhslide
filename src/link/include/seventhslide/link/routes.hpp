#pragma once

// How good a way each connection is to fetch media through, and how fast this
// computer sends it.
//
// A connection's route is whether the computer at its end is on this network
// (heard there, at the address it is connected at) and how fast media has come
// over it (``Throughput``). Media goes to the clearly better of two: one on this
// network before one that is not, and else one measured at least twice as fast.
// Two about as good are alike, so measurements that wander do not move work to
// and fro.
//
// The upload speed cap (``UploadCap``) holds what this computer sends of media to
// computers not on this network to a rate: a church's uplink keeps room for its
// livestream. Change files and member list entries are small, and go as they
// always do, so edits are never held behind it.
//
// Seam: plain C++20.

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>

namespace seventhslide::link {

using Clock = std::chrono::steady_clock;

/// How fast media comes over one connection, measured only while some is
/// expected: a connection with nothing to fetch keeps what it was last measured
/// at, rather than seem slow for having been idle.
class Throughput {
public:
    /// ``bytes`` came over the last ``seconds``; ``expecting``: media was asked
    /// for and its bytes were due all that while.
    void sample(std::int64_t bytes, double seconds, bool expecting);
    /// Bytes a second, once measured over a few seconds of fetching, or over
    /// enough bytes: a fast connection may be done in less.
    [[nodiscard]] std::optional<double> per_second() const;

    /// How many seconds of fetching a measure is over, at most.
    static constexpr double k_window_seconds = 10;
    /// How many it takes to have one, or how many bytes.
    static constexpr double k_measured_seconds = 3;
    static constexpr std::int64_t k_measured_bytes = 16 * 1024 * 1024;

private:
    struct Sample {
        std::int64_t bytes = 0;
        double seconds = 0;
    };
    std::deque<Sample> samples_;
    double seconds_ = 0;
    std::int64_t bytes_ = 0;
};

struct Route {
    /// The computer is on this network.
    bool close = false;
    /// Bytes a second of media come over it, once measured.
    std::optional<double> per_second;
};

/// How much faster one route must be measured for the other to count as worse.
inline constexpr double k_clearly_faster = 2.0;

/// Whether ``a`` is clearly the better way to fetch media: on this network where
/// ``b`` is not, or alike so and measured at least ``k_clearly_faster`` times as
/// fast. Neither is for two about alike, or one not measured yet.
[[nodiscard]] bool clearly_better(const Route& a, const Route& b);

/// What this computer may send of media, held to a rate over all its
/// connections. What goes ahead of the rate is owed, and nothing more goes
/// until it is made up, so a frame larger than a moment's worth still goes
/// whole, and the rate holds on average.
class UploadCap {
public:
    /// Bytes a second; 0 for no cap.
    void set_rate(std::int64_t bytes_per_second, Clock::time_point now = Clock::now());
    [[nodiscard]] std::int64_t rate() const noexcept { return rate_; }
    /// Whether ``bytes`` may go now: then they are counted as gone.
    bool take(std::int64_t bytes, Clock::time_point now = Clock::now());
    /// How long until something may go: zero when it may now.
    [[nodiscard]] std::chrono::milliseconds wait(Clock::time_point now = Clock::now()) const;
    /// The size of the frames media goes in under the cap: a few to a second's
    /// worth, so the uplink is used in short bursts, not long ones. A power of
    /// two from 16 KiB up to ``largest``, which it divides.
    [[nodiscard]] std::int64_t frame(std::int64_t largest) const;

private:
    /// What may go, in bytes: below zero for what went ahead.
    [[nodiscard]] double allowance(Clock::time_point now) const;

    std::int64_t rate_ = 0;
    double allowance_ = 0;
    Clock::time_point at_{};
};

/// The cap as it is set, in kilobits a second (``SyncSettings::upload_cap_kbit``),
/// in bytes a second for ``UploadCap``.
[[nodiscard]] constexpr std::int64_t upload_cap_bytes(std::int64_t kbit) noexcept
{
    return kbit > 0 ? kbit * 1000 / 8 : 0;
}

}  // namespace seventhslide::link
