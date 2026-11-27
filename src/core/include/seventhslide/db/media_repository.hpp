#pragma once

// Each media kind's registry (``<kind>_files``) and folder tree.
// Seam: Qt SQL via Database. Disk unlink is the caller's job.

#include "seventhslide/db/database.hpp"
#include "seventhslide/db/folder_tree.hpp"
#include "seventhslide/db/media_kind.hpp"
#include "seventhslide/model/library.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace seventhslide::db {

class MediaRepository {
public:
    explicit MediaRepository(Database& db) : db_(db) {}

    // --- <kind>_files: the files each kind's library holds ---
    /// Register ``filename`` as titled ``display_name``, or retitle it when it
    /// is registered already (its ``library_visible`` stays).
    void upsert_file(MediaKind kind, std::string_view filename, std::string_view display_name);
    [[nodiscard]] std::optional<model::MediaFile> get_file(MediaKind kind,
                                                           std::string_view filename);
    /// By title.
    [[nodiscard]] std::vector<model::MediaFile> list_files(MediaKind kind,
                                                           bool visible_only = true);
    void set_file_visible(MediaKind kind, std::string_view filename, bool visible);
    /// Hard-delete the registry row. Returns true if a row was removed.
    bool remove_file(MediaKind kind, std::string_view filename);
    /// Retitle; the file on disk keeps its name.
    void set_display_name(MediaKind kind, std::string_view filename,
                          std::string_view display_name);
    /// The audio fades audio file ``filename`` sets for itself: only the
    /// fields it sets (``model::set_audio_fades``); empty for none, or for a
    /// file not registered.
    [[nodiscard]] nlohmann::json audio_fades(std::string_view filename);
    /// Keep ``fades`` (only the fields it sets) as audio file ``filename``'s
    /// own; an empty object gives it back to the service and the outputs.
    void set_audio_fades(std::string_view filename, const nlohmann::json& fades);
    /// Every audio file that sets fades of its own, by filename.
    [[nodiscard]] std::map<std::string, nlohmann::json, std::less<>> audio_fades_by_file();

    /// The members of ``filenames`` that something shows: an image or image
    /// folder service item, or a Freeform service or library item (image
    /// objects and slide backgrounds). Reads each row once for the whole set.
    [[nodiscard]] std::unordered_set<std::string> referenced_images(
        const std::vector<std::string>& filenames);
    // --- service items that play a video or audio file by name ---
    // ``kind`` is Video or Audio; its service items have the matching
    // item_type and keep the file in ``data.filename``. Such files appear
    // nowhere else (Freeform documents hold images only).

    /// How many service items play ``filename``.
    [[nodiscard]] int count_playback_refs(MediaKind kind, std::string_view filename);
    /// The members of ``filenames`` some service item plays. One pass over the
    /// service items however many filenames are asked about: deleting a folder
    /// asks about every file in it.
    [[nodiscard]] std::unordered_set<std::string> referenced_playback_files(
        MediaKind kind, const std::vector<std::string>& filenames);

    /// Point everything that uses ``old_name`` at ``new_name`` instead:
    /// the service items that play or show it, image folder items' lists,
    /// Freeform documents (in services and in the library), and ``kind``'s
    /// folder links, each keeping its place, and an audio file's own fades.
    /// The registry rows are the caller's. Returns how many rows changed. All of it or none: it is a
    /// transaction of its own, or a savepoint inside the caller's (where the
    /// registry change it goes with belongs).
    std::size_t retarget_file(MediaKind kind, std::string_view old_name, std::string_view new_name);

    // --- folders (each kind its own tree) ---
    /// The ``kind`` folders. Deleting one goes through ``delete_folder_tree``.
    [[nodiscard]] FolderTree folders(MediaKind kind) const;
    /// Deletes folder + descendants. Their folder_items CASCADE with the folders.
    /// Returns filenames that were linked (for orphan cleanup by caller).
    std::vector<std::string> delete_folder_tree(MediaKind kind, std::int64_t id);
    /// Filenames linked anywhere in the folder tree rooted at ``id``. Read-only.
    [[nodiscard]] std::vector<std::string> list_folder_tree_filenames(MediaKind kind,
                                                                      std::int64_t id);
    /// Every filename currently linked in any folder of this kind.
    [[nodiscard]] std::unordered_set<std::string> list_linked_filenames(MediaKind kind);

    // --- folder items ---
    [[nodiscard]] std::int64_t add_folder_item(MediaKind kind,
                                               std::int64_t folder_id,
                                               std::string_view filename);
    /// folder id -> number of items in it, for every folder of ``kind``.
    /// One query instead of one per folder.
    [[nodiscard]] std::unordered_map<std::int64_t, int> folder_item_counts(MediaKind kind);

    [[nodiscard]] std::vector<model::MediaFolderItem> list_folder_items(MediaKind kind,
                                                                        std::int64_t folder_id);
    /// Put the items in ``folder_id`` in the order of ``ordered_item_ids``,
    /// writing only those that move (``list_order.hpp``).
    void reorder_folder_items(MediaKind kind,
                              std::int64_t folder_id,
                              const std::vector<std::int64_t>& ordered_item_ids);
    /// Remove every link to ``filename`` from ``kind``'s folders.
    void unlink_filename_from_folders(MediaKind kind, std::string_view filename);
    /// Unlink ``filename`` from all folders, then optionally link into ``folder_id``.
    /// ``folder_id`` nullopt = leave loose (root).
    void move_filename_to_folder(MediaKind kind,
                                 std::string_view filename,
                                 std::optional<std::int64_t> folder_id);
    /// ``move_filename_to_folder`` for each of ``filenames``, all or none.
    void move_filenames_to_folder(MediaKind kind,
                                  const std::vector<std::string>& filenames,
                                  std::optional<std::int64_t> folder_id);

private:
    Database& db_;
};

}  // namespace seventhslide::db
