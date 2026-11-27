#pragma once

// Content DTOs: Song, Service, ServiceItem, BiblePassage, StyleProfile.
// Plain C++20 + nlohmann/json (no Qt).

#include "seventhslide/model/author.hpp"
#include "seventhslide/model/content_kind.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::model {

/// Topics a song may have, and the bytes one may take.
inline constexpr std::size_t k_max_song_topics = 24;
inline constexpr std::size_t k_max_song_topic_bytes = 40;

/// ``topics`` as stored: tidied, without repeats (ignoring case), as typed.
[[nodiscard]] std::vector<std::string> normalize_song_topics(const std::vector<std::string>& topics);

/// Library / import song. Its JSON (``to_json`` / ``from_json``) is the
/// content alone - no row id - and is what a service item stores for a song.
struct Song {
    std::optional<std::int64_t> id;
    std::string title;
    std::string lyrics;
    std::string verse_order;
    std::vector<Author> authors;
    std::string songbook_name;
    std::string songbook_entry;
    nlohmann::json theme_map = nlohmann::json::object();
    std::string copyright;
    std::string ccli_song_number;
    bool show_copyright = false;
    std::string key;
    /// Words that group songs (Christmas, Communion, Second Coming), as typed
    /// and tidied by normalize_song_topics.
    std::vector<std::string> topics;
    /// The operator's own notes on the song; never shown on screen.
    std::string comments;

    [[nodiscard]] nlohmann::json to_json() const;
    static Song from_json(const nlohmann::json& j);

    [[nodiscard]] bool operator==(const Song&) const = default;
};

/// Tags a service may have, and the bytes one may take.
inline constexpr std::size_t k_max_service_tags = 24;
inline constexpr std::size_t k_max_service_tag_bytes = 40;

/// ``tags`` as stored: tidied, without repeats (ignoring case), as typed.
[[nodiscard]] std::vector<std::string> normalize_service_tags(const std::vector<std::string>& tags);

struct Service {
    std::optional<std::int64_t> id;
    std::string name;
    nlohmann::json theme_map = nlohmann::json::object();
    /// Words that group services (Communion, Youth), as typed and tidied by
    /// normalize_service_tags.
    std::vector<std::string> tags;
    /// Local ISO ``yyyy-MM-ddTHH:mm:ss``; empty if unset.
    std::string created_at;
    /// When the service is held, same format as ``created_at``; empty for a
    /// service with no date.
    std::string service_at;
    /// The audio fades it sets for every audio item while it is open, over
    /// each output's (``set_audio_fades``: only the fields it sets).
    nlohmann::json audio_fades = nlohmann::json::object();
};

/// A tag and how many services carry it.
struct TagCount {
    std::string name;
    int count = 0;

    [[nodiscard]] bool operator==(const TagCount&) const = default;
};

/// The tags ``services`` use, each once (the first spelling met), by name
/// ignoring case.
[[nodiscard]] std::vector<TagCount> service_tag_counts(const std::vector<Service>& services);

/// Known service-item type strings (the persisted form of ContentKind).
inline constexpr std::string_view k_item_type_song = to_string(ContentKind::Song);
inline constexpr std::string_view k_item_type_bible = to_string(ContentKind::Bible);
inline constexpr std::string_view k_item_type_freeform = to_string(ContentKind::Freeform);
inline constexpr std::string_view k_item_type_video = to_string(ContentKind::Video);
inline constexpr std::string_view k_item_type_audio = to_string(ContentKind::Audio);
inline constexpr std::string_view k_item_type_image = to_string(ContentKind::Image);
inline constexpr std::string_view k_item_type_image_folder = to_string(ContentKind::ImageFolder);
inline constexpr std::string_view k_item_type_web = to_string(ContentKind::Web);
inline constexpr std::string_view k_item_type_live = to_string(ContentKind::Live);
inline constexpr std::string_view k_item_type_divider = to_string(ContentKind::Divider);

/// The data an image live item or rundown row carries: the file and its
/// title (the library's name for it; the filename is opaque).
[[nodiscard]] nlohmann::json image_item_data(std::string_view filename, std::string_view title);
/// The data a video or audio live item or rundown row carries: the file,
/// its title, and how it plays.
[[nodiscard]] nlohmann::json playback_item_data(std::string_view filename,
                                                std::string_view title,
                                                bool loop = false);

/// One ordered service row. ``data`` is the durable snapshot of what it plays
/// and the only copy: everything else is read out of it when asked for.
struct ServiceItem {
    std::optional<std::int64_t> item_id;
    std::optional<std::int64_t> song_id;
    std::int64_t order_num = 0;
    std::string item_type = std::string(k_item_type_song);
    nlohmann::json data = nlohmann::json::object();

    [[nodiscard]] ContentKind kind() const noexcept
    {
        return content_kind_from_string(item_type);
    }

    /// The song a song row plays: its snapshot, with ``song_id``.
    [[nodiscard]] Song song() const;
    /// What the rundown calls the row; never empty.
    [[nodiscard]] std::string title() const;
    /// The row's own theme choices (none for Freeform, whose document has them).
    [[nodiscard]] nlohmann::json theme_map() const;
    /// A song row edited apart from its library song, or any other row with
    /// theme choices of its own.
    [[nodiscard]] bool has_overrides() const;
};

[[nodiscard]] inline std::string service_item_stack_id(const ServiceItem& item)
{
    if (item.data.is_object() && item.data.contains("stack_id") && item.data["stack_id"].is_string()) {
        return item.data["stack_id"].get<std::string>();
    }
    return {};
}

/// Other rundown rows that go live with this item, each on its own stack.
[[nodiscard]] inline std::vector<std::int64_t> service_item_also_play(const ServiceItem& item)
{
    std::vector<std::int64_t> ids;
    if (!item.data.is_object() || !item.data.contains("also_play")) {
        return ids;
    }
    const auto& v = item.data["also_play"];
    auto push = [&](std::int64_t id) {
        if (id < 0) {
            return;
        }
        for (auto existing : ids) {
            if (existing == id) {
                return;
            }
        }
        ids.push_back(id);
    };
    if (v.is_number_integer()) {
        push(v.get<std::int64_t>());
        return ids;
    }
    if (!v.is_array()) {
        return ids;
    }
    for (const auto& e : v) {
        if (e.is_number_integer()) {
            push(e.get<std::int64_t>());
        }
    }
    return ids;
}

inline void set_service_item_also_play(ServiceItem& item, std::vector<std::int64_t> ids)
{
    if (!item.data.is_object()) {
        item.data = nlohmann::json::object();
    }
    std::vector<std::int64_t> unique;
    unique.reserve(ids.size());
    for (auto id : ids) {
        if (id < 0) {
            continue;
        }
        bool seen = false;
        for (auto existing : unique) {
            if (existing == id) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            unique.push_back(id);
        }
    }
    if (unique.empty()) {
        item.data.erase("also_play");
    } else {
        item.data["also_play"] = std::move(unique);
    }
}

/// Result of ``parse_bible_reference`` (verse_* nullopt = whole chapter).
struct BibleRef {
    std::string book;
    int chapter = 0;
    std::optional<int> verse_start;
    std::optional<int> verse_end;
};

/// One verse of a passage.
struct PassageVerse {
    int verse_num = 0;
    std::string text;

    [[nodiscard]] bool operator==(const PassageVerse&) const = default;
};

/// A passage as it plays live and as a service item stores it (its JSON).
struct BiblePassage {
    std::optional<std::int64_t> bible_id;
    std::string book;
    int chapter = 0;
    int verse_start = 0;
    int verse_end = 0;
    std::string ref;
    /// Translation / bible name for the ref overlay.
    std::string version;
    nlohmann::json theme_map = nlohmann::json::object();
    /// The verses, in order; stored as ``[{verse_num, text}, ...]``.
    std::vector<PassageVerse> verses;

    [[nodiscard]] nlohmann::json to_json() const;
    static BiblePassage from_json(const nlohmann::json& j);
};

/// Named style-profile snapshot (output category defaults, song theme maps).
struct StyleProfile {
    std::optional<std::int64_t> id;
    std::string name;
    std::int64_t sort_order = 0;
    /// ``{outputs: {output_id: category_defaults}, songs: {song_id: theme_map}, ...}``
    nlohmann::json data = nlohmann::json::object();

    [[nodiscard]] static nlohmann::json empty_data()
    {
        return nlohmann::json{
            {"outputs", nlohmann::json::object()},
            {"songs", nlohmann::json::object()},
        };
    }
};

} // namespace seventhslide::model
