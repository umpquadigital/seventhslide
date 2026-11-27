#include "seventhslide/stream/stream_settings.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace seventhslide::stream {
namespace {

template <typename E, std::size_t N>
std::optional<E> from_name(const std::array<std::pair<E, std::string_view>, N>& names,
                           std::string_view name)
{
    for (const auto& [value, text] : names) {
        if (text == name) {
            return value;
        }
    }
    return std::nullopt;
}

template <typename E, std::size_t N>
std::string_view to_name(const std::array<std::pair<E, std::string_view>, N>& names, E value)
{
    for (const auto& [v, text] : names) {
        if (v == value) {
            return text;
        }
    }
    return names.front().second;
}

constexpr std::array<std::pair<SrtMode, std::string_view>, 2> k_modes{{
    {SrtMode::Listener, "listener"},
    {SrtMode::Caller, "caller"},
}};
constexpr std::array<std::pair<StreamPreset, std::string_view>, 2> k_presets{{
    {StreamPreset::LowDelay, "low_delay"},
    {StreamPreset::Reliable, "reliable"},
}};
constexpr std::array<std::pair<VideoCodec, std::string_view>, 2> k_codecs{{
    {VideoCodec::H265, "h265"},
    {VideoCodec::H264, "h264"},
}};

/// SRT's own limits on a passphrase.
constexpr std::size_t k_passphrase_min = 10;
constexpr std::size_t k_passphrase_max = 79;

/// Kept within what a network and a receiver can reasonably take.
constexpr int k_min_bitrate_kbps = 500;
constexpr int k_max_bitrate_kbps = 50'000;

}  // namespace

std::string_view mode_name(SrtMode mode)
{
    return to_name(k_modes, mode);
}

std::optional<SrtMode> mode_from_name(std::string_view name)
{
    return from_name(k_modes, name);
}

std::string_view preset_name(StreamPreset preset)
{
    return to_name(k_presets, preset);
}

std::optional<StreamPreset> preset_from_name(std::string_view name)
{
    return from_name(k_presets, name);
}

std::string_view codec_name(VideoCodec codec)
{
    return to_name(k_codecs, codec);
}

std::optional<VideoCodec> codec_from_name(std::string_view name)
{
    return from_name(k_codecs, name);
}

std::string validate(const StreamSettings& settings)
{
    if (settings.port < 1 || settings.port > 65535) {
        return "The port must be a number from 1 to 65535.";
    }
    // Only a caller's is used. One colon is a port typed into the address;
    // IPv6 has several.
    if (settings.mode == SrtMode::Caller
        && (settings.host.find_first_of(" /?#@") != std::string::npos
            || std::count(settings.host.begin(), settings.host.end(), ':') == 1)) {
        return "Enter the address as a host name or IP address, without srt:// or a port.";
    }
    if (!settings.passphrase.empty()
        && (settings.passphrase.size() < k_passphrase_min
            || settings.passphrase.size() > k_passphrase_max)) {
        return "A passphrase must be 10 to 79 characters long.";
    }
    if (settings.bitrate_kbps != 0
        && (settings.bitrate_kbps < k_min_bitrate_kbps
            || settings.bitrate_kbps > k_max_bitrate_kbps)) {
        return "The bitrate must be from 0.5 to 50 Mb/s.";
    }
    return {};
}

std::string missing_to_send(const StreamSettings& settings)
{
    if (settings.mode == SrtMode::Caller && settings.host.empty()) {
        return "Enter the address of the receiver to send to.";
    }
    return {};
}

int latency_ms(StreamPreset preset)
{
    // Low delay: SRT's own default, a few round trips on a LAN. Reliable:
    // enough for several resends across the internet.
    return preset == StreamPreset::LowDelay ? 120 : 1500;
}

int keyframe_seconds(StreamPreset preset)
{
    return preset == StreamPreset::LowDelay ? 1 : 2;
}

int auto_bitrate_kbps(VideoCodec codec, StreamPreset preset, int width, int height)
{
    // What 1080p at 30 fps wants: H.265 does about as well as H.264 at two
    // thirds of the bitrate; the reliable preset spends less of the link.
    int full_hd = 0;
    if (preset == StreamPreset::LowDelay) {
        full_hd = codec == VideoCodec::H265 ? 10'000 : 15'000;
    } else {
        full_hd = codec == VideoCodec::H265 ? 4'000 : 6'000;
    }
    const std::int64_t area = std::int64_t{std::max(width, 0)} * std::max(height, 0);
    if (area == 0) {
        return full_hd;
    }
    const std::int64_t scaled = full_hd * area / (1920 * 1080);
    return static_cast<int>(std::clamp<std::int64_t>(scaled, k_min_bitrate_kbps,
                                                     k_max_bitrate_kbps));
}

int bitrate_kbps(const StreamSettings& settings, int width, int height)
{
    return settings.bitrate_kbps > 0
               ? settings.bitrate_kbps
               : auto_bitrate_kbps(settings.codec, settings.preset, width, height);
}

std::string address_text(const StreamSettings& settings)
{
    const std::string port = std::to_string(settings.port);
    if (settings.mode == SrtMode::Listener) {
        return "srt://:" + port;
    }
    const bool v6 = settings.host.find(':') != std::string::npos;
    return "srt://" + (v6 ? "[" + settings.host + "]" : settings.host) + ":" + port;
}

}  // namespace seventhslide::stream
