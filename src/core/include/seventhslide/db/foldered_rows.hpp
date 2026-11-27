#pragma once

// Rows of their own kept in a folder tree: Freeform items and web pages. A row
// is in one folder at a time (its ``folder_id``; NULL for the library's root),
// in the order its ``sort_order`` gives, and goes when its folder is deleted.
//
// Only that shared handling is here. What a row holds, how one is made, read
// and written, and what else goes when one is deleted (a Freeform item's
// thumbnails) stay with its repository and its controller: a Freeform item
// and a web page are different things that happen to be filed alike.
//
// Image, video and audio folders are not like this: they list files by name,
// and one file can be in several folders (``MediaRepository``).
//
// Qt SQL seam.

#include "seventhslide/db/database.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace seventhslide::db {

class FolderedRows {
public:
    /// ``table`` holds the rows and ``folder_table`` their folders; both are
    /// names from the code, never user input. ``noun`` names one row in
    /// error messages ("web page").
    FolderedRows(Database& db, std::string_view table, std::string_view folder_table,
                 std::string_view noun);

    /// How many rows each folder holds, for the folders that hold any.
    [[nodiscard]] std::unordered_map<std::int64_t, int> counts() const;
    /// Row ``id`` into ``folder`` (nullopt: the root), at its end. False when
    /// there is no such row.
    bool move(std::int64_t id, std::optional<std::int64_t> folder);
    /// Each of ``ids`` into ``folder``, at its end in that order, all or none.
    /// Returns how many there were.
    int move_all(const std::vector<std::int64_t>& ids, std::optional<std::int64_t> folder);
    /// ``folder``'s rows in the order ``ids`` gives; only those that move are
    /// written.
    void reorder(std::optional<std::int64_t> folder, const std::vector<std::int64_t>& ids);
    /// False when there is no such row.
    bool remove(std::int64_t id);
    /// Folder ``id``, the folders under it and every row in them, all or
    /// none. Returns the rows' ids, for what goes with them.
    std::vector<std::int64_t> delete_folder_tree(std::int64_t id);

private:
    Database& db_;
    std::string table_;
    std::string folder_table_;
    std::string noun_;
};

}  // namespace seventhslide::db
