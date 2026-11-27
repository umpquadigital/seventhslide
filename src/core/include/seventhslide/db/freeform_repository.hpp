#pragma once

// Freeform library folders and items.

#include "seventhslide/db/database.hpp"
#include "seventhslide/db/folder_tree.hpp"
#include "seventhslide/db/foldered_rows.hpp"
#include "seventhslide/model/library.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace seventhslide::db {

/// A browser row. No document: the thumb URL is built from the stored key.
struct FreeformBrowserRow {
    std::int64_t id = 0;
    std::string name;
    std::string thumb_output_id;
    std::string thumb_key;
};

class FreeformRepository {
public:
    explicit FreeformRepository(Database& db) : db_(db) {}

    // --- folders ---
    /// The Freeform folders. Deleting one goes through ``delete_folder_tree``.
    [[nodiscard]] FolderTree folders() const;
    /// Deletes folder + descendants and items in the tree. Returns deleted item ids.
    std::vector<std::int64_t> delete_folder_tree(std::int64_t id);

    // --- items ---
    [[nodiscard]] std::int64_t create_item(model::FreeformItem& item);
    void update_item(const model::FreeformItem& item);
    [[nodiscard]] std::optional<model::FreeformItem> get_item(std::int64_t id);
    /// Items whose ``folder_id`` is ``folder_id`` (nullopt = library root).
    [[nodiscard]] std::vector<model::FreeformItem> list_items_in_folder(
        std::optional<std::int64_t> folder_id);
    /// Every item, whatever its folder, documents included: for a walk over
    /// all of them (an output's canvas changing).
    [[nodiscard]] std::vector<model::FreeformItem> list_items();
    /// Same rows as ``list_items_in_folder``, without the document column.
    [[nodiscard]] std::vector<FreeformBrowserRow> list_browser_rows(
        std::optional<std::int64_t> folder_id);
    /// Item count of every folder, in one query. Root items (null folder) are
    /// omitted; they are not a folder row.
    [[nodiscard]] std::unordered_map<std::int64_t, int> folder_item_counts();
    /// Move an item into ``new_folder_id`` (nullopt = library root). Appends at end.
    bool move_item(std::int64_t id, std::optional<std::int64_t> new_folder_id);
    /// Every item of ``ids`` into ``new_folder_id``, at its end in that order,
    /// all or none. Returns how many there were.
    int move_items(const std::vector<std::int64_t>& ids, std::optional<std::int64_t> new_folder_id);
    /// Put the items in ``folder_id`` (nullopt = root) in that order, writing
    /// only those that move (``list_order.hpp``).
    void reorder_items(std::optional<std::int64_t> folder_id,
                       const std::vector<std::int64_t>& ordered_item_ids);
    bool remove_item(std::int64_t id);

private:
    /// How items are filed: shared with the web pages (``FolderedRows``).
    [[nodiscard]] FolderedRows rows() const;

    Database& db_;
};

}  // namespace seventhslide::db
