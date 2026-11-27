#pragma once

// A live video source - an NDI source on the network, later a capture card, a
// DeckLink input or a stream - as a service item and a stack remember it: by
// its kind and its name, not by anything that lasts only while it is on. A
// camera that is off when the service is built shows as soon as it is on.
// Plain C++20 (no Qt).

#include <nlohmann/json_fwd.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::model {

/// The kinds of live source: NDI on the network, capture devices (a webcam,
/// a capture card) and DeckLink inputs on this computer, and streams added
/// by hand (SRT, RTMP, RTSP, HLS, ...).
inline constexpr std::string_view k_live_kind_ndi = "ndi";
inline constexpr std::string_view k_live_kind_device = "device";
inline constexpr std::string_view k_live_kind_decklink = "decklink";
inline constexpr std::string_view k_live_kind_stream = "stream";

struct LiveSource {
    /// ``k_live_kind_ndi``, ...
    std::string kind;
    /// As the source names itself: NDI's ``HOST (Camera 1)``; a stream's,
    /// as the operator named it.
    std::string name;
    /// A stream's address (``srt://host:9000``); empty for the other kinds,
    /// which are known by name.
    std::string url;

    [[nodiscard]] bool empty() const noexcept { return kind.empty() || name.empty(); }
    [[nodiscard]] bool operator==(const LiveSource&) const = default;
};

/// What the operator typed, as a stream's address: trimmed, its scheme
/// lower-cased, and one of SRT, RTMP(S), RTSP(S), RIST, UDP, RTP, HTTP(S)
/// (HLS). nullopt for anything else, or for text with spaces in it.
[[nodiscard]] std::optional<std::string> stream_url_from_text(std::string_view text);

/// A stream's address as it may be shown (a stream's name when the operator
/// gave none, in the list, the service, the Controller, the remote): without
/// what lets someone else send or watch it. A password before the host,
/// secret-looking query values (``passphrase``, ``key``, ``token``, ...) and
/// an RTMP stream key (the path after the application) become "•••"; a
/// fragment goes. The address itself keeps them, to be read with.
[[nodiscard]] std::string stream_display_name(std::string_view url);

/// What a live source's service item or live item carries.
[[nodiscard]] nlohmann::json live_item_data(const LiveSource& source);
/// The source ``data`` (``live_item_data``'s) names; empty when it names none
/// of a kind this build knows. A stream with no name of its own (none, or its
/// address) is named ``stream_display_name`` of its address.
[[nodiscard]] LiveSource live_source_from_item_data(const nlohmann::json& data);
/// The key its pictures go by among the outputs' sources: ``ndi:HOST (Cam)``;
/// a stream's by its address, ``stream:srt://host:9000``.
[[nodiscard]] std::string live_source_key(const LiveSource& source);

} // namespace seventhslide::model
