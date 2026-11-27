#pragma once

// Library DTOs for media, bible, and Freeform folders/items.
// Plain C++20 (no Qt).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace seventhslide::model {

/// A row of a media kind's registry (``<kind>_files``): a file the library
/// holds because it was imported.
struct MediaFile {
    std::string filename; // on-disk key / primary key
    std::string display_name;
    bool library_visible = true;

    /// What the library calls it: the display name, else the filename.
    [[nodiscard]] const std::string& title() const noexcept
    {
        return display_name.empty() ? filename : display_name;
    }
};

/// A nestable folder: image, video and Freeform folders all have this shape.
struct Folder {
    std::optional<std::int64_t> id;
    std::string name;
    std::int64_t sort_order = 0;
    std::optional<std::int64_t> parent_id;

    [[nodiscard]] bool operator==(const Folder&) const = default;
};

/// A folder with its full path from the root ("Parent / Child").
struct FolderPath {
    std::int64_t id = 0;
    std::string name;
    std::string path;
};

/// Every folder in ``folders`` (one table's) with its path, sorted by path.
/// A parent missing from the list, or a cycle, ends that path early.
[[nodiscard]] std::vector<FolderPath> folder_paths(const std::vector<Folder>& folders);

struct MediaFolderItem {
    std::optional<std::int64_t> id;
    std::int64_t folder_id = 0;
    std::string filename;
    std::int64_t sort_order = 0;
};

/// Bytes a Bible's abbreviation may take ("KJV", "NIV 2011").
inline constexpr std::size_t k_max_bible_abbreviation_bytes = 12;

struct Bible {
    std::optional<std::int64_t> id;
    std::string name;
    std::string copyright;
    /// ISO-ish timestamp from SQLite DEFAULT; empty if unset.
    std::string created_at;
    /// Short name for tight places ("KJV"); empty when none was given.
    std::string abbreviation;
    /// Offered for lookup. A switched-off Bible keeps its verses but is not
    /// listed where translations are picked (the Bibles tab, the web remote).
    bool enabled = true;
};

/// ``raw`` as a Bible abbreviation: one line, trimmed, at most
/// k_max_bible_abbreviation_bytes.
[[nodiscard]] std::string normalize_bible_abbreviation(std::string_view raw);

struct BibleVerse {
    std::optional<std::int64_t> id;
    std::int64_t bible_id = 0;
    std::string book;
    int chapter = 0;
    int verse_num = 0;
    std::string text;
};

struct FreeformItem {
    std::optional<std::int64_t> id;
    std::string name;
    std::optional<std::int64_t> folder_id;
    std::int64_t sort_order = 0;
    /// Freeform ``Document`` JSON (see ``freeform::load_document``).
    nlohmann::json document = nlohmann::json::object();
    std::vector<std::string> tags;
    /// Deck the library thumb is taken from, and the content key of its first
    /// slide. Written when the document is saved so a browser row does not
    /// parse ``document``. Canvas size is applied when the URL is built.
    std::string thumb_output_id;
    std::string thumb_key;
    /// Its name on every computer, and the owner of its own files
    /// (freeform/own_media.hpp). Read back; a new item's is given to
    /// ``FreeformRepository::create_item`` on its own.
    std::string uid;
};

/// A rundown row's ``data`` for Freeform ``item``: its name and document.
[[nodiscard]] nlohmann::json freeform_item_data(const FreeformItem& item);

}  // namespace seventhslide::model
