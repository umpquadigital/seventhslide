#include "seventhslide/db/media_listing.hpp"

#include "seventhslide/db/search_text.hpp"

#include <optional>
#include <unordered_map>
#include <utility>

namespace seventhslide::db {
MediaListing list_media(MediaRepository& repo,
                        MediaKind kind,
                        std::optional<std::int64_t> folder,
                        std::string_view query)
{
    const SearchQuery search(query);
    const auto matches = [&search](std::string_view text) { return search.matches(text); };
    MediaListing out;

    for (auto& f : repo.folders(kind).children(folder)) {
        if (f.id && matches(f.name)) {
            out.folders.push_back(std::move(f));
        }
    }

    if (folder) {
        // One pass over the registry instead of a lookup per folder item.
        std::unordered_map<std::string, model::MediaFile> by_filename;
        for (auto& file : repo.list_files(kind, false)) {
            by_filename.emplace(file.filename, std::move(file));
        }
        for (const auto& item : repo.list_folder_items(kind, *folder)) {
            const auto found = by_filename.find(item.filename);
            if (found == by_filename.end() || !found->second.library_visible) {
                continue;
            }
            const auto& file = found->second;
            if (!matches(file.title()) && !matches(file.filename)) {
                continue;
            }
            out.files.push_back({item.id ? *item.id : -1, file.filename, file.title()});
        }
        return out;
    }

    // The root lists only unfiled files; the rest stay inside their folders.
    const auto linked = repo.list_linked_filenames(kind);
    for (auto& file : repo.list_files(kind, true)) {
        if (linked.contains(file.filename)) {
            continue;
        }
        if (!matches(file.title()) && !matches(file.filename)) {
            continue;
        }
        auto title = file.title();
        out.files.push_back({-1, std::move(file.filename), std::move(title)});
    }
    return out;
}

}  // namespace seventhslide::db
