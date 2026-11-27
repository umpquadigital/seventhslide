#pragma once

// What the media library shows at one folder, for one search box.
//
// The operator console and the phone remote browse the same library and must
// agree on the rules - which files a folder holds, that the root shows only
// unfiled ones, that a file needs a visible registry row. Those rules live
// here so there is one copy to change.
//
// Seam: MediaRepository (Qt SQL).

#include "seventhslide/db/media_kind.hpp"
#include "seventhslide/db/media_repository.hpp"
#include "seventhslide/model/library.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

struct MediaListingFile {
    /// folder_items row id, or -1 for a file listed loose at the root.
    std::int64_t item_id = -1;
    std::string filename;
    /// The registry's display name, else the filename.
    std::string title;
};

struct MediaListing {
    std::vector<model::Folder> folders;
    std::vector<MediaListingFile> files;
};

/**
 * Folders and files to show at ``folder`` (nullopt = the root).
 *
 * ``query`` filters by case-insensitive substring against a folder's name and
 * against both a file's title and its filename; empty matches everything.
 */
[[nodiscard]] MediaListing list_media(MediaRepository& repo,
                                      MediaKind kind,
                                      std::optional<std::int64_t> folder,
                                      std::string_view query);

}  // namespace seventhslide::db
