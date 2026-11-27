#include "seventhslide/link/routes.hpp"

#include <algorithm>

namespace seventhslide::link {
namespace {

/// What a cap lets gather while nothing goes: a quarter of a second's worth, so
/// an idle while is not made up for in one long burst.
constexpr double k_most_saved_seconds = 0.25;
constexpr std::int64_t k_least_frame = 16 * 1024;

}  // namespace

void Throughput::sample(std::int64_t bytes, double seconds, bool expecting)
{
    if (!expecting || seconds <= 0) {
        return;
    }
    samples_.push_back({bytes, seconds});
    bytes_ += bytes;
    seconds_ += seconds;
    while (seconds_ - samples_.front().seconds >= k_window_seconds) {
        bytes_ -= samples_.front().bytes;
        seconds_ -= samples_.front().seconds;
        samples_.pop_front();
    }
}

std::optional<double> Throughput::per_second() const
{
    if (seconds_ <= 0 || (seconds_ < k_measured_seconds && bytes_ < k_measured_bytes)) {
        return std::nullopt;
    }
    return static_cast<double>(bytes_) / seconds_;
}

bool clearly_better(const Route& a, const Route& b)
{
    if (a.close != b.close) {
        return a.close;
    }
    return a.per_second && b.per_second && *a.per_second >= k_clearly_faster * *b.per_second;
}

double UploadCap::allowance(Clock::time_point now) const
{
    const double seconds = std::chrono::duration<double>(now - at_).count();
    const double most = static_cast<double>(rate_) * k_most_saved_seconds;
    return std::min(most, allowance_ + static_cast<double>(rate_) * std::max(0.0, seconds));
}

void UploadCap::set_rate(std::int64_t bytes_per_second, Clock::time_point now)
{
    rate_ = std::max<std::int64_t>(0, bytes_per_second);
    allowance_ = 0;
    at_ = now;
}

bool UploadCap::take(std::int64_t bytes, Clock::time_point now)
{
    if (rate_ == 0) {
        return true;
    }
    const auto may = allowance(now);
    if (may < 0) {
        return false;
    }
    allowance_ = may - static_cast<double>(bytes);
    at_ = now;
    return true;
}

std::chrono::milliseconds UploadCap::wait(Clock::time_point now) const
{
    if (rate_ == 0) {
        return std::chrono::milliseconds(0);
    }
    const auto may = allowance(now);
    if (may >= 0) {
        return std::chrono::milliseconds(0);
    }
    // Rounded up: woken a moment early, it would only wait again.
    const auto ms = static_cast<std::int64_t>(-may * 1000 / static_cast<double>(rate_)) + 1;
    return std::chrono::milliseconds(ms);
}

std::int64_t UploadCap::frame(std::int64_t largest) const
{
    if (rate_ == 0) {
        return largest;
    }
    std::int64_t frame = k_least_frame;
    while (frame * 2 <= rate_ / 8 && frame * 2 <= largest) {
        frame *= 2;
    }
    return std::min(frame, largest);
}

}  // namespace seventhslide::link
