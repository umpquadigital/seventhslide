#pragma once

// A web page saved in the library's Browser tab: shown on the outputs as a
// picture of the page, drawn at its own size and zoom whatever size it is
// shown at.
// Plain C++20 (no Qt).

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace seventhslide::model {

inline constexpr int k_web_default_width = 1920;
inline constexpr int k_web_default_height = 1080;
/// A side the page is drawn at: from a phone's width to 8K.
inline constexpr int k_web_min_side = 160;
inline constexpr int k_web_max_side = 7680;
/// Pages with more pixels than 4K (3840 x 2160) may be drawn, but each
/// picture of one costs more than a computer can be counted on to spare.
inline constexpr int k_web_heavy_pixels = 3840 * 2160;
inline constexpr double k_web_min_zoom = 0.25;
inline constexpr double k_web_max_zoom = 5.0;

struct WebPage {
    std::optional<std::int64_t> id;
    std::string name;
    std::string url;
    /// The size the page is drawn at, in CSS pixels times ``zoom``.
    int width = k_web_default_width;
    int height = k_web_default_height;
    double zoom = 1.0;
    std::optional<std::int64_t> folder_id;
    std::int64_t sort_order = 0;

    [[nodiscard]] bool operator==(const WebPage&) const = default;
};

/// What the operator typed, as the page to load: trimmed, given ``https://``
/// when it names no scheme, or ``file://`` when it is a path: from ``/``,
/// from a Windows drive (``C:\Users\me\wall.html``, either slash) or on a
/// Windows share (``\\server\share\wall.html``, the server the URL's host).
/// A path's spaces, ``%``, ``?`` and ``#`` are escaped: they are the file's
/// name. Only http and https pages that name their site, and files; nullopt
/// for anything else, or for text with spaces in it that is not a path.
[[nodiscard]] std::optional<std::string> web_url_from_text(std::string_view text);

/// ``page`` with its size and zoom in range and its name trimmed; an empty
/// name becomes the URL's host (or the URL).
[[nodiscard]] WebPage normalized(WebPage page);

/// What a web page's service item or live item carries: a snapshot of the
/// page (``page_id``, ``name``, ``url``, ``width``, ``height``, ``zoom``), so
/// it plays as it was added even if the library's page changes or goes.
[[nodiscard]] nlohmann::json web_item_data(const WebPage& page);
/// The page ``data`` (``web_item_data``'s) describes, normalized; its url is
/// empty when ``data`` names none a page can show.
[[nodiscard]] WebPage web_page_from_item_data(const nlohmann::json& data);

} // namespace seventhslide::model
