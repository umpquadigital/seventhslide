#pragma once

// How an SRT destination streams: where to (or which port to wait on), how
// much delay it may trade for reliability, which video codec, and at what
// bitrate. The rules that turn those few choices into encoder and SRT
// settings live here, so the editor offers and the streamer uses the same.
// Seam: plain C++20 (no Qt, no FFmpeg).

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::stream {

/// Pictures a second every stream is sent at: the destinations' own cadence.
inline constexpr int k_frame_rate = 30;
/// The port a new destination waits on, or counts up from.
inline constexpr int k_default_port = 9000;

/// Who opens the connection.
enum class SrtMode {
    /// This computer waits on a port; a receiver (OBS, vMix, a decoder)
    /// connects to it.
    Listener,
    /// This computer connects out to a receiver waiting at an address.
    Caller,
};

/// What the stream trades: delay against how much loss it survives and how
/// much network it takes.
enum class StreamPreset {
    /// A local network: little buffering, a higher bitrate, a keyframe each
    /// second.
    LowDelay,
    /// The internet or a weak link: a second and a half of buffering for
    /// resending what is lost, a lower bitrate, a keyframe every two seconds.
    Reliable,
};

enum class VideoCodec { H265, H264 };

struct StreamSettings {
    SrtMode mode = SrtMode::Listener;
    /// Caller: the receiver's host name or address. A listener waits on
    /// every address this computer has, and keeps this for a switch back.
    std::string host;
    int port = k_default_port;
    StreamPreset preset = StreamPreset::LowDelay;
    VideoCodec codec = VideoCodec::H265;
    /// 0 picks one for the preset, codec and size (``auto_bitrate_kbps``).
    int bitrate_kbps = 0;
    /// Encrypts the stream; the receiver must give the same. Empty sends it
    /// unencrypted. SRT takes 10 to 79 characters.
    std::string passphrase;

    [[nodiscard]] bool operator==(const StreamSettings&) const = default;
};

[[nodiscard]] std::string_view mode_name(SrtMode mode);
[[nodiscard]] std::optional<SrtMode> mode_from_name(std::string_view name);
[[nodiscard]] std::string_view preset_name(StreamPreset preset);
[[nodiscard]] std::optional<StreamPreset> preset_from_name(std::string_view name);
[[nodiscard]] std::string_view codec_name(VideoCodec codec);
[[nodiscard]] std::optional<VideoCodec> codec_from_name(std::string_view name);

/// What is wrong with ``settings``, for the operator; empty when nothing.
/// Settings may be right and still not be enough to send: see
/// ``missing_to_send``.
[[nodiscard]] std::string validate(const StreamSettings& settings);
/// What ``settings`` still need before anything can be sent (a caller's
/// address), for the operator; empty when nothing. Kept apart from
/// ``validate`` so the editor can save a change of mode before the address
/// it then asks for.
[[nodiscard]] std::string missing_to_send(const StreamSettings& settings);

/// How long a receiver waits for what was lost to be sent again, the
/// stream's delay over the network's own.
[[nodiscard]] int latency_ms(StreamPreset preset);
/// Seconds between keyframes: how soon a receiver that joins (or loses a
/// picture) has one again.
[[nodiscard]] int keyframe_seconds(StreamPreset preset);
/// The bitrate ``codec`` is given at ``width`` x ``height`` under ``preset``
/// when the operator leaves it automatic: in proportion to the picture's
/// area, from what 1080p wants.
[[nodiscard]] int auto_bitrate_kbps(VideoCodec codec, StreamPreset preset, int width, int height);
/// The bitrate ``settings`` streams at for ``width`` x ``height``.
[[nodiscard]] int bitrate_kbps(const StreamSettings& settings, int width, int height);

/// Where the stream goes or waits, as a receiver is told it:
/// "srt://studio.local:9000" for a caller, "srt://:9000" for a listener
/// (on every address).
[[nodiscard]] std::string address_text(const StreamSettings& settings);

}  // namespace seventhslide::stream
