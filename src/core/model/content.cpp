#include <seventhslide/model/content.hpp>

#include "seventhslide/json/read.hpp"
#include <seventhslide/model/rich_text.hpp>
#include "seventhslide/util/strings.hpp"
#include "seventhslide/util/word_list.hpp"

#include <algorithm>
#include <string_view>
#include <unordered_map>

namespace seventhslide::model {
namespace {
using seventhslide::json::array_or_empty;
using seventhslide::json::bool_or;
using seventhslide::json::int_or;
using seventhslide::json::object_or_empty;
using seventhslide::json::str_or;


std::optional<std::int64_t> opt_int64(const nlohmann::json& j, const char* key)
{
    if (!j.contains(key) || j[key].is_null()) {
        return std::nullopt;
    }
    if (j[key].is_number_integer()) {
        return j[key].get<std::int64_t>();
    }
    return std::nullopt;
}

} // namespace

// ---- Song -------------------------------------------------------------------

nlohmann::json Song::to_json() const
{
    return {
        {"title", title},
        {"lyrics", lyrics},
        {"verse_order", verse_order},
        {"authors", authors_to_json(authors)},
        {"songbook_name", songbook_name},
        {"songbook_entry", songbook_entry},
        {"theme_map", theme_map.is_object() ? theme_map : nlohmann::json::object()},
        {"copyright", copyright},
        {"ccli_song_number", ccli_song_number},
        {"show_copyright", show_copyright},
        {"key", key},
        {"topics", topics},
        {"comments", comments},
    };
}

Song Song::from_json(const nlohmann::json& j)
{
    Song s;
    if (!j.is_object()) {
        return s;
    }
    s.title = str_or(j, "title");
    s.lyrics = str_or(j, "lyrics");
    s.verse_order = str_or(j, "verse_order");
    if (const auto* authors = json::find(j, "authors")) {
        s.authors = authors_from_json(*authors);
    }
    // Credits are names and numbers: surrounding whitespace is never meant.
    s.songbook_name = util::trim(str_or(j, "songbook_name"));
    s.songbook_entry = util::trim(str_or(j, "songbook_entry"));
    s.theme_map = object_or_empty(j, "theme_map");
    s.copyright = util::trim(str_or(j, "copyright"));
    s.ccli_song_number = util::trim(str_or(j, "ccli_song_number"));
    s.show_copyright = bool_or(j, "show_copyright", false);
    s.key = util::trim(str_or(j, "key"));
    s.topics = normalize_song_topics(json::strings_or_empty(j, "topics"));
    s.comments = str_or(j, "comments");
    return s;
}

std::vector<std::string> normalize_service_tags(const std::vector<std::string>& tags)
{
    return util::normalize_word_list(tags, {.max_words = k_max_service_tags,
                                            .max_bytes = k_max_service_tag_bytes,
                                            .lower_case = false});
}

std::vector<TagCount> service_tag_counts(const std::vector<Service>& services)
{
    std::vector<TagCount> out;
    std::unordered_map<std::string, std::size_t> at;
    for (const auto& service : services) {
        for (const auto& tag : service.tags) {
            const auto key = util::to_lower(tag);
            if (const auto it = at.find(key); it != at.end()) {
                ++out[it->second].count;
            } else {
                at.emplace(key, out.size());
                out.push_back({tag, 1});
            }
        }
    }
    std::ranges::sort(out, [](const TagCount& a, const TagCount& b) {
        return util::to_lower(a.name) < util::to_lower(b.name);
    });
    return out;
}

std::vector<std::string> normalize_song_topics(const std::vector<std::string>& topics)
{
    return util::normalize_word_list(topics, {.max_words = k_max_song_topics,
                                              .max_bytes = k_max_song_topic_bytes,
                                              .lower_case = false});
}

// ---- ServiceItem ------------------------------------------------------------

Song ServiceItem::song() const
{
    Song song = Song::from_json(data);
    song.id = song_id;
    return song;
}

std::string ServiceItem::title() const
{
    std::string out;
    switch (kind()) {
    case ContentKind::Song:
        out = str_or(data, "title");
        break;
    case ContentKind::Bible:
        out = str_or(data, "ref", "Bible Verse");
        break;
    case ContentKind::Freeform:
        // The name with markup tags stripped, trimmed.
        out = util::trim(strip_markup_tags(str_or(data, "name")));
        if (out.empty()) {
            out = "Freeform";
        }
        break;
    case ContentKind::Video:
        out = str_or(data, "title");
        if (out.empty()) {
            out = str_or(data, "filename", "Video");
        }
        break;
    case ContentKind::Audio:
        out = str_or(data, "title");
        if (out.empty()) {
            out = str_or(data, "filename", "Audio");
        }
        break;
    case ContentKind::Image:
        out = str_or(data, "title");
        if (out.empty()) {
            out = str_or(data, "filename", "Image");
        }
        break;
    case ContentKind::ImageFolder:
        out = str_or(data, "folder_name", "Image Folder");
        break;
    case ContentKind::Web:
        out = str_or(data, "name", "Web page");
        break;
    case ContentKind::Live:
        out = str_or(data, "name", "Live source");
        break;
    case ContentKind::Divider:
        out = str_or(data, "title", "Section");
        break;
    case ContentKind::Empty:
        break;
    }
    return out.empty() ? item_type : out;
}

nlohmann::json ServiceItem::theme_map() const
{
    return kind() == ContentKind::Freeform ? nlohmann::json::object()
                                           : object_or_empty(data, "theme_map");
}

bool ServiceItem::has_overrides() const
{
    switch (kind()) {
    case ContentKind::Song:
        return bool_or(data, "user_modified", true);
    case ContentKind::Freeform:
        return false;
    case ContentKind::Bible:
    case ContentKind::Video:
    case ContentKind::Audio:
    case ContentKind::Image:
    case ContentKind::ImageFolder:
    case ContentKind::Web:
    case ContentKind::Live:
    case ContentKind::Divider:
    case ContentKind::Empty:
        break;
    }
    return !theme_map().empty();
}

nlohmann::json image_item_data(std::string_view filename, std::string_view title)
{
    return {{"filename", filename}, {"title", title.empty() ? filename : title}};
}

nlohmann::json playback_item_data(std::string_view filename, std::string_view title, bool loop)
{
    return {{"filename", filename},
            {"title", title.empty() ? filename : title},
            {"autoplay", true},
            {"loop", loop}};
}

// ---- Bible ------------------------------------------------------------------

nlohmann::json BiblePassage::to_json() const
{
    nlohmann::json j{
        {"book", book},
        {"chapter", chapter},
        {"verse_start", verse_start},
        {"verse_end", verse_end},
        {"ref", ref},
        {"version", version},
        {"theme_map", theme_map.is_object() ? theme_map : nlohmann::json::object()},
        {"verses", nlohmann::json::array()},
    };
    auto& rows = j["verses"];
    for (const auto& v : verses) {
        rows.push_back({{"verse_num", v.verse_num}, {"text", v.text}});
    }
    if (bible_id) {
        j["bible_id"] = *bible_id;
    }
    return j;
}

BiblePassage BiblePassage::from_json(const nlohmann::json& j)
{
    BiblePassage p;
    if (!j.is_object()) {
        return p;
    }
    p.bible_id = opt_int64(j, "bible_id");
    p.book = str_or(j, "book");
    p.chapter = int_or(j, "chapter", 0);
    p.verse_start = int_or(j, "verse_start", 0);
    p.verse_end = int_or(j, "verse_end", 0);
    p.ref = str_or(j, "ref");
    p.version = str_or(j, "version");
    p.theme_map = object_or_empty(j, "theme_map");
    for (const auto& row : array_or_empty(j, "verses")) {
        // A row that is not an object still counts, as an empty verse.
        p.verses.push_back({int_or(row, "verse_num", 0), str_or(row, "text")});
    }
    return p;
}

} // namespace seventhslide::model
