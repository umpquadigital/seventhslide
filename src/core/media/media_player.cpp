#include "seventhslide/media/media_player.hpp"

#include <cmath>
#include <cstdio>

namespace seventhslide::media {

namespace {

[[nodiscard]] double shown_seconds(const IMediaPlayer* player, double sec) noexcept
{
    return player != nullptr && player->has_media() && std::isfinite(sec) && sec > 0.0 ? sec
                                                                                      : 0.0;
}

}  // namespace

double shown_position_sec(const IMediaPlayer* player) noexcept
{
    return player != nullptr ? shown_seconds(player, player->position_sec()) : 0.0;
}

double shown_duration_sec(const IMediaPlayer* player) noexcept
{
    return player != nullptr ? shown_seconds(player, player->duration_sec()) : 0.0;
}

std::string format_video_countdown(double position_sec, double duration_sec)
{
    if (!(duration_sec > 0.0) || !std::isfinite(duration_sec) || !std::isfinite(position_sec)) {
        return {};
    }
    // currentTime - duration gives the negative remaining time.
    const double delta = position_sec - duration_sec;
    const int total = static_cast<int>(std::ceil(std::abs(delta)));
    const int minutes = total / 60;
    const int seconds = total % 60;
    char buf[16];
    if (delta < 0.0) {
        std::snprintf(buf, sizeof(buf), "-%02d:%02d", minutes, seconds);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes, seconds);
    }
    return std::string(buf);
}

} // namespace seventhslide::media
