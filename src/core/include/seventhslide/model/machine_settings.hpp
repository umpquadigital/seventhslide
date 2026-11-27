#pragma once

// Settings that belong to this computer rather than to the library open on it.
//
// library::MachineStore keeps them in the app data dir, never in a library,
// so they stay with the computer when a library is copied or synced to
// another. The web remote's are here because the remote is this computer's
// server: its port is one this computer's network allows, and it serves
// whichever library is open.
//
// Seam: plain C++20 + nlohmann/json (no Qt).

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace seventhslide::model {

/// Whether the web remote listens (default off).
inline constexpr std::string_view k_setting_remote_enabled = "remote_enabled";
/// Optional PIN; empty means the remote needs none.
inline constexpr std::string_view k_setting_remote_pin = "remote_pin";
/// TCP port the remote listens on when enabled.
inline constexpr std::string_view k_setting_remote_port = "remote_port";
/// How receivers on other computers take this one's NDI sources.
inline constexpr std::string_view k_setting_ndi_transport = "ndi_transport";

/// Whether changes from other computers wait while something is live
/// (default on).
inline constexpr std::string_view k_setting_sync_pause_live = "sync_pause_live";
/// How long the Controller's Fade takes (Transition's menu), in ms.
inline constexpr std::string_view k_setting_take_fade_ms = "take_fade_ms";

/// The take's Fade: half a second until set, and from a tenth of a second
/// to ten seconds.
inline constexpr int k_default_take_fade_ms = 500;
inline constexpr int k_min_take_fade_ms = 100;
inline constexpr int k_max_take_fade_ms = 10000;

/// Clamp a stored / entered Fade length into its range.
[[nodiscard]] constexpr int normalize_take_fade_ms(int ms) noexcept
{
    return ms < k_min_take_fade_ms ? k_min_take_fade_ms
         : ms > k_max_take_fade_ms ? k_max_take_fade_ms
                                   : ms;
}

/// Default remote port - also the fallback when a stored value is out of range.
inline constexpr int k_default_remote_port = 1844;

/// Clamp a stored / user-entered port into the usable non-privileged range.
[[nodiscard]] inline int normalize_remote_port(int port) noexcept
{
    return (port >= 1024 && port <= 65535) ? port : k_default_remote_port;
}

/// How this computer's NDI sources are sent to receivers on others.
enum class NdiTransport {
    /// Whatever the NDI runtime prefers (reliable UDP, today), falling back to
    /// whatever else a receiver takes.
    Automatic,
    /// Reliable UDP and nothing else.
    ReliableUdp,
    /// Plain UDP: nothing sent again, so a lost packet is lost.
    Udp,
    /// Several TCP connections per receiver, so one held-up packet does not
    /// hold up everything behind it.
    MultiTcp,
    /// One TCP connection per receiver, pictures and sound together: for
    /// networks UDP does not get through, as some Wi-Fi.
    SingleTcp,
};

/// ``"auto"`` / ``"rudp"`` / ``"udp"`` / ``"multi_tcp"`` / ``"tcp"``, as
/// stored. ``"tcp"`` is Single TCP, the one TCP there was at first.
[[nodiscard]] std::string_view ndi_transport_key(NdiTransport transport) noexcept;
/// The transport ``key`` names; anything else reads as Automatic.
[[nodiscard]] NdiTransport ndi_transport_from_key(std::string_view key) noexcept;

struct MachineSettings {
    bool remote_enabled = false;
    std::string remote_pin;
    int remote_port = k_default_remote_port;
    bool sync_pause_live = true;
    NdiTransport ndi_transport = NdiTransport::Automatic;
    /// How long a take by Fade takes, in place of the theme's transition
    /// (Transition's menu, which also offers Cut). It suits this room's
    /// operator, not the library.
    int take_fade_ms = k_default_take_fade_ms;

    /// Setting key -> JSON value.
    [[nodiscard]] nlohmann::json to_json() const;
    /// Reads only the keys it knows, so a library's whole ``app_settings``
    /// object reads as the machine settings it still holds.
    static MachineSettings from_json(const nlohmann::json& j);

    [[nodiscard]] bool operator==(const MachineSettings&) const = default;
};

}  // namespace seventhslide::model
