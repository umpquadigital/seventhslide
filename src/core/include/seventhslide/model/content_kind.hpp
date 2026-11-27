#pragma once

// The one vocabulary for "what kind of content is this".
// Persisted item_type strings, scene mode, build kind, theme category and
// stack-type keys are views of ContentKind, not separate enums to keep in sync.

#include <cstdint>
#include <string_view>

namespace seventhslide::model {

enum class ContentKind : std::uint8_t {
    Empty,
    Song,
    Bible,
    Freeform,
    Image,
    ImageFolder,
    Video,
    Audio,
    /// A web page (``model::WebPage``), drawn off screen and shown like an image.
    Web,
    /// A live video source (``model::LiveSource``): NDI, later capture,
    /// DeckLink, streams.
    Live,
    Divider,
};

/// Wire / database form. Empty has no item_type string.
[[nodiscard]] constexpr std::string_view to_string(ContentKind kind) noexcept
{
    switch (kind) {
    case ContentKind::Song:
        return "song";
    case ContentKind::Bible:
        return "bible";
    case ContentKind::Freeform:
        return "freeform";
    case ContentKind::Image:
        return "image";
    case ContentKind::ImageFolder:
        return "image_folder";
    case ContentKind::Video:
        return "video";
    case ContentKind::Audio:
        return "audio";
    case ContentKind::Web:
        return "web";
    case ContentKind::Live:
        return "live";
    case ContentKind::Divider:
        return "divider";
    case ContentKind::Empty:
        return "";
    }
    return "";
}

/// Unknown and empty strings are Empty.
[[nodiscard]] constexpr ContentKind content_kind_from_string(std::string_view s) noexcept
{
    if (s == "song") {
        return ContentKind::Song;
    }
    if (s == "bible") {
        return ContentKind::Bible;
    }
    if (s == "freeform") {
        return ContentKind::Freeform;
    }
    if (s == "image") {
        return ContentKind::Image;
    }
    if (s == "image_folder") {
        return ContentKind::ImageFolder;
    }
    if (s == "video") {
        return ContentKind::Video;
    }
    if (s == "audio") {
        return ContentKind::Audio;
    }
    if (s == "web") {
        return ContentKind::Web;
    }
    if (s == "live") {
        return ContentKind::Live;
    }
    if (s == "divider") {
        return ContentKind::Divider;
    }
    return ContentKind::Empty;
}

} // namespace seventhslide::model
