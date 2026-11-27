#pragma once

// The operations every folder hierarchy in the library shares: image, video
// and audio folders, Freeform folders, web folders. Each lives in its own
// table with ``id``, ``name``, ``sort_order`` and a self-referencing
// ``parent_id`` that cascades on delete. What a folder *holds* differs per
// table and stays with its repository.
//
// Qt SQL seam.

#include "seventhslide/db/database.hpp"
#include "seventhslide/model/library.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

class FolderTree {
public:
    /// ``table`` is a folder table name from the code, never user input.
    FolderTree(Database& db, std::string_view table);

    /// A new folder at the end of ``parent``'s children (the root when unset).
    std::int64_t create(std::string_view name, std::optional<std::int64_t> parent = std::nullopt);

    /// False when there is no such folder.
    bool rename(std::int64_t id, std::string_view name);

    /// Re-parent ``id`` to the end of ``parent``'s children. False when either
    /// folder does not exist or ``parent`` is ``id`` itself or inside it.
    bool move(std::int64_t id, std::optional<std::int64_t> parent);

    /// Give ``parent``'s children ``ordered_ids`` order. Ids that are not
    /// children of ``parent`` are left alone.
    void reorder_children(std::optional<std::int64_t> parent,
                          const std::vector<std::int64_t>& ordered_ids);

    /// The folder ``id``, or nullopt when there is none.
    [[nodiscard]] std::optional<model::Folder> get(std::int64_t id) const;
    /// Every folder, in sort order.
    [[nodiscard]] std::vector<model::Folder> list() const;
    /// ``parent``'s children (the root's when unset), in sort order.
    [[nodiscard]] std::vector<model::Folder> children(std::optional<std::int64_t> parent) const;

    /// ``id`` plus every folder under it.
    [[nodiscard]] std::vector<std::int64_t> subtree_ids(std::int64_t id) const;

    /// Delete ``id``; ``parent_id`` cascades, so its subtree goes with it.
    /// False when there is no such folder.
    bool remove(std::int64_t id);

private:
    Database& db_;
    std::string table_;
};

}  // namespace seventhslide::db
