// SyncEngine: the media files the library's records own.
//
// One slice of SyncEngine (declared in sync_engine.hpp): which files the
// records here own (files_owned, media_here), which are not on the disk
// (missing_media), what the size limit takes and what it keeps here
// (media_use, title_of, held_back), and a file no record owns any more
// taken off the disk (remove_unowned).
// Seam: db (the records and the media declarations) and the media folder.

#include "seventhslide/sync/sync_engine.hpp"

#include "sync_engine_internal.hpp"

#include <algorithm>
#include <set>

namespace seventhslide::sync {

std::vector<std::string> SyncEngine::files_owned()
{
    std::vector<std::string> out;
    for (const auto& f : db::k_sync_files) {
        for (const auto& key : db::record_keys(db_, f.table)) {
            // A file's record owns it by its key; a record naming files by
            // URL (an output) is read for them.
            const auto paths = f.in == db::FileIn::Key
                                   ? files_of(f.table, key, json())
                                   : files_of(f.table, key,
                                              db::read_record(db_, f.table, key).value_or(json()));
            out.insert(out.end(), paths.begin(), paths.end());
        }
    }
    std::ranges::sort(out);
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

SyncEngine::MediaHere SyncEngine::media_here()
{
    MediaHere here;
    here.owned = files_owned();
    for (auto& digest : book_.media_digests()) {
        auto path = digest.path;
        here.declared.emplace(std::move(path), std::move(digest));
    }
    return here;
}

std::vector<db::MediaDigest> SyncEngine::missing_media(const MediaUse& use)
{
    return missing_media(use, media_here());
}

std::vector<db::MediaDigest> SyncEngine::missing_media(const MediaUse& use, const MediaHere& here)
{
    std::set<std::string> over;
    for (const auto& o : use.over) {
        over.insert(o.path);
    }
    std::vector<db::MediaDigest> out;
    for (const auto& path : here.owned) {
        std::error_code ec;
        if (!over.contains(path) && !std::filesystem::exists(media_ / path, ec)) {
            const auto declared = here.declared.find(path);
            out.push_back(declared != here.declared.end() ? declared->second
                                                          : db::MediaDigest{path, 0, {}, std::nullopt});
        }
    }
    return out;
}

MediaUse SyncEngine::media_use()
{
    return media_use(media_here());
}

MediaUse SyncEngine::media_use(const MediaHere& here)
{
    struct File {
        std::string path;
        std::int64_t size = 0;
        bool declared = false;
        std::optional<db::SyncVersion> version;
    };
    std::vector<File> files;
    for (const auto& path : here.owned) {
        if (const auto declared = here.declared.find(path); declared != here.declared.end()) {
            files.push_back({path, declared->second.size, true, declared->second.version});
        } else {
            // Made here and not sent yet (or lost before it was): as it is on
            // the disk.
            std::error_code ec;
            const auto size = std::filesystem::file_size(media_ / path, ec);
            files.push_back({path, ec ? 0 : static_cast<std::int64_t>(size), false, std::nullopt});
        }
    }
    // Declared first, the earliest first; then those made here, by name.
    std::ranges::sort(files, [](const File& a, const File& b) {
        if (a.declared != b.declared) {
            return a.declared;
        }
        if (a.version != b.version) {
            return a.version < b.version;
        }
        return a.path < b.path;
    });
    MediaUse use;
    use.limit = size_limit_;
    for (const auto& f : files) {
        if (size_limit_ <= 0 || use.used + f.size <= size_limit_) {
            use.used += f.size;
            continue;
        }
        std::error_code ec;
        use.over.push_back({f.path, f.size, title_of(f.path),
                            !f.declared && std::filesystem::exists(media_ / f.path, ec)});
    }
    return use;
}

std::string SyncEngine::title_of(const std::string& path)
{
    const auto slash = path.find('/');
    const auto folder = path.substr(0, slash);
    const auto name = path.substr(slash + 1);
    for (const auto& f : db::k_sync_files) {
        if (f.folder == folder && f.in == db::FileIn::Key) {
            if (const auto record = db::read_record(db_, f.table, name)) {
                const auto title = record->value("display_name", std::string());
                if (!title.empty()) {
                    return title;
                }
            }
        }
    }
    return name;
}

std::set<SyncEngine::RecordName> SyncEngine::held_back(const MediaUse& use)
{
    std::set<std::string> paths;
    std::set<std::string> names;
    for (const auto& o : use.over) {
        if (!book_.media_digest(o.path)) {
            paths.insert(o.path);
            names.insert(o.path.substr(o.path.find('/') + 1));
        }
    }
    std::set<RecordName> out;
    if (paths.empty()) {
        return out;
    }
    for (const auto& [table, key] : marked_now()) {
        if (db::owns_files_by_url(table)) {
            continue;
        }
        const auto current = db::read_record(db_, table, key);
        if (!current) {
            continue;
        }
        // Owning it, or naming it (a folder's entry for it, a service item).
        const bool hold = std::ranges::any_of(files_of(table, key, *current),
                                              [&](const std::string& p) { return paths.contains(p); })
                          || std::ranges::any_of(db::names_in(table, key, *current),
                                                 [&](const std::string& n) { return names.contains(n); });
        if (hold) {
            out.emplace(table, key);
        }
    }
    return out;
}

void SyncEngine::remove_unowned()
{
    auto paths = std::move(unowned_);
    unowned_.clear();
    if (paths.empty()) {
        return;
    }
    // Read once, however many went: an output names its files in its JSON.
    const auto owned = files_owned();
    for (const auto& path : paths) {
        if (!std::ranges::binary_search(owned, path)) {
            std::error_code ec;
            std::filesystem::remove(media_ / path, ec);
            pieces_.forget(path);
        }
    }
}

}  // namespace seventhslide::sync
