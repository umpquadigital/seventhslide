#pragma once

// Song library persistence.
// Seam: Qt SQL via Database; domain type is model::Song.

#include "seventhslide/db/database.hpp"
#include "seventhslide/model/content.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace seventhslide::db {

class SongRepository {
public:
    explicit SongRepository(Database& db) : db_(db) {}

    /// Insert a song; sets ``song.id`` and returns it.
    [[nodiscard]] std::int64_t insert(model::Song& song);

    /// Full-column update. Requires ``song.id``.
    void update(const model::Song& song);

    [[nodiscard]] std::optional<model::Song> get(std::int64_t id);

    /// All songs ordered by title, then id, with ``lyrics`` left empty
    /// (picker / summary).
    [[nodiscard]] std::vector<model::Song> list_summaries();
    /// ``list_summaries`` with the lyrics: what the library's search reads.
    [[nodiscard]] std::vector<model::Song> list_searchable();
    /// Likewise only the songs with these uids (those still here), in no
    /// order: what sync changed.
    [[nodiscard]] std::vector<model::Song> list_searchable(const std::set<std::string>& uids);
    /// Every song's id, lowest first.
    [[nodiscard]] std::vector<std::int64_t> list_ids();

    /// Detach service-item snapshots first, then delete the library row.
    /// Returns true if a row was deleted.
    bool remove(std::int64_t id);

    /// Bulk theme_map write (style-profile apply). Keys are song ids.
    void set_theme_maps(const std::vector<std::pair<std::int64_t, nlohmann::json>>& maps);

    /// Bulk verse_order write (style-profile apply). Keys are song ids.
    void set_verse_orders(const std::vector<std::pair<std::int64_t, std::string>>& orders);

private:
    Database& db_;
};

}  // namespace seventhslide::db
