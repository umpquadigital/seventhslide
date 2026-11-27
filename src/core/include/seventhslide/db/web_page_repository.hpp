#pragma once

// The Browser tab's web pages and their folders.
// Seam: Qt SQL via Database.

#include "seventhslide/db/database.hpp"
#include "seventhslide/db/folder_tree.hpp"
#include "seventhslide/db/foldered_rows.hpp"
#include "seventhslide/model/web_page.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace seventhslide::db {

class WebPageRepository {
public:
    explicit WebPageRepository(Database& db) : db_(db) {}

    // --- folders ---
    [[nodiscard]] FolderTree folders() const;
    /// Deletes the folder, its folders and every page in them. Returns the
    /// pages' ids.
    std::vector<std::int64_t> delete_folder_tree(std::int64_t id);

    // --- pages ---
    /// Stores ``page`` (normalized) at the end of its folder; sets its id.
    std::int64_t create_item(model::WebPage& page);
    /// Writes ``page``'s name, URL, size and zoom (normalized). Its folder and
    /// place are moved by ``move_item`` and ``reorder_items``. False when it
    /// is gone.
    bool update_item(const model::WebPage& page);
    [[nodiscard]] std::optional<model::WebPage> get_item(std::int64_t id);
    /// The pages in ``folder_id`` (nullopt = the root), in order.
    [[nodiscard]] std::vector<model::WebPage> list_items_in_folder(
        std::optional<std::int64_t> folder_id);
    /// Every page, in no particular order.
    [[nodiscard]] std::vector<model::WebPage> all_items();
    /// Page count of every folder that has any.
    [[nodiscard]] std::unordered_map<std::int64_t, int> folder_item_counts();
    /// Into ``folder_id`` (nullopt = the root), at its end.
    bool move_item(std::int64_t id, std::optional<std::int64_t> folder_id);
    /// Every page of ``ids`` into ``folder_id``, at its end in that order, all
    /// or none. Returns how many there were.
    int move_items(const std::vector<std::int64_t>& ids, std::optional<std::int64_t> folder_id);
    /// The pages of ``folder_id`` in this order; only those that move are written.
    void reorder_items(std::optional<std::int64_t> folder_id,
                       const std::vector<std::int64_t>& ordered_ids);
    bool remove_item(std::int64_t id);

private:
    /// How pages are filed: shared with Freeform items (``FolderedRows``).
    [[nodiscard]] FolderedRows rows() const;

    Database& db_;
};

}  // namespace seventhslide::db
