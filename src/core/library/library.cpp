#include "seventhslide/library/library.hpp"

#include "seventhslide/util/atomic_file.hpp"
#include "seventhslide/util/find.hpp"
#include "seventhslide/util/read_file.hpp"
#include "seventhslide/util/strings.hpp"

#include <algorithm>
#include <string>
#include <system_error>
#include <utility>

namespace seventhslide::library {
namespace {

namespace fs = std::filesystem;

constexpr int k_registry_version = 1;

[[nodiscard]] fs::path normalized(const fs::path& p)
{
    std::error_code ec;
    auto abs = fs::weakly_canonical(p, ec);
    if (ec || abs.empty()) {
        abs = p.lexically_normal();
    }
    return abs;
}

}  // namespace

fs::path database_file(const fs::path& root)
{
    return root / std::string{k_database_filename};
}

fs::path media_root(const fs::path& root)
{
    return root / std::string{k_media_dirname};
}

fs::path sync_root(const fs::path& root)
{
    return root / std::string{k_sync_dirname};
}

bool is_local_only(const fs::path& relative)
{
    const auto path = relative.lexically_normal();
    const auto leaf = path.filename().string();
    // Anywhere: a write's temp file, a file an import is still copying.
    if (leaf.ends_with(".tmp")
        || (leaf.size() > k_staging_prefix.size() + k_staging_suffix.size()
            && leaf.starts_with(k_staging_prefix) && leaf.ends_with(k_staging_suffix))) {
        return true;
    }
    auto part = path.begin();
    if (part == path.end()) {
        return false;
    }
    const auto first = part->string();
    if (first == k_sync_dirname) {
        return true;
    }
    if (++part == path.end()) {
        // At the root, beside the database.
        const std::string db(k_database_filename);
        return first == db + "-wal" || first == db + "-shm" || first == db + "-journal";
    }
    return first == k_media_dirname && part->string() == k_thumbs_dirname;
}

std::string slugify(std::string_view name)
{
    std::string out;
    out.reserve(name.size());
    bool pending_dash = false;
    for (const char c : name) {
        if (util::is_alnum(c)) {
            if (pending_dash && !out.empty()) {
                out.push_back('-');
            }
            pending_dash = false;
            out.push_back(util::to_lower(c));
        } else {
            // Collapse every run of punctuation / whitespace into one dash, and
            // never emit a leading or trailing one.
            pending_dash = true;
        }
    }
    if (out.size() > 48) {
        out.resize(48);
    }
    return out;
}

nlohmann::json LibraryEntry::to_json() const
{
    return nlohmann::json{
        {"id", id},
        {"name", name},
        {"path", root.generic_string()},
    };
}

std::optional<LibraryEntry> LibraryEntry::from_json(const nlohmann::json& j)
{
    if (!j.is_object()) {
        return std::nullopt;
    }
    LibraryEntry entry;
    if (const auto it = j.find("id"); it != j.end() && it->is_string()) {
        entry.id = util::trim(it->get<std::string>());
    }
    if (const auto it = j.find("name"); it != j.end() && it->is_string()) {
        entry.name = util::trim(it->get<std::string>());
    }
    if (const auto it = j.find("path"); it != j.end() && it->is_string()) {
        entry.root = fs::path{it->get<std::string>()};
    }
    if (entry.id.empty() || entry.root.empty()) {
        return std::nullopt;
    }
    if (entry.name.empty()) {
        entry.name = entry.id;
    }
    return entry;
}

fs::path LibraryEntry::database_file() const
{
    return library::database_file(root);
}

fs::path LibraryEntry::media_root() const
{
    return library::media_root(root);
}

LibraryRegistry::LibraryRegistry(fs::path data_dir)
    : data_dir_(std::move(data_dir))
{
}

fs::path LibraryRegistry::registry_file() const
{
    return data_dir_ / std::string{k_registry_filename};
}

void LibraryRegistry::load()
{
    entries_.clear();
    active_id_.clear();
    load_error_.clear();

    const auto file = registry_file();
    const auto refuse = [&](const std::string& why,
                            std::string_view advice = "put it right, or move it aside to start a "
                                                      "new list, then open SeventhSlide again") {
        entries_.clear();
        active_id_.clear();
        load_error_ = "The library list " + file.string() + " " + why + ". It is left as it is: "
                      + std::string(advice) + ".";
    };

    const auto text = util::read_file(file);
    if (!text) {
        // Not there is a first run; there but not readable is refused.
        std::error_code ec;
        if (fs::exists(file, ec) || ec) {
            refuse("cannot be read");
        }
        return;
    }
    const nlohmann::json doc = nlohmann::json::parse(*text, nullptr, /*allow_exceptions=*/false);
    if (!doc.is_object() || !doc.contains("version") || !doc.contains("libraries")
        || !doc.at("libraries").is_array()) {
        refuse("is damaged");
        return;
    }
    if (const auto& version = doc.at("version"); version != k_registry_version) {
        refuse("is version " + version.dump() + ", which this build does not read",
               "open it with the newer SeventhSlide that wrote it");
        return;
    }
    // Every entry or none: one dropped would be lost at the next save.
    for (const auto& raw : doc.at("libraries")) {
        auto entry = LibraryEntry::from_json(raw);
        if (!entry) {
            refuse("lists a library without its id or folder");
            return;
        }
        if (id_taken(entry->id)) {
            refuse("lists the library \"" + entry->id + "\" twice");
            return;
        }
        entries_.push_back(std::move(*entry));
    }
    if (const auto it = doc.find("active"); it != doc.end() && it->is_string()) {
        active_id_ = it->get<std::string>();
    }
}

bool LibraryRegistry::save() const
{
    // The file load refused may list every library this computer has.
    if (!load_error_.empty()) {
        return false;
    }
    nlohmann::json doc{
        {"version", k_registry_version},
        {"active", active_id_},
        {"libraries", nlohmann::json::array()},
    };
    for (const auto& entry : entries_) {
        doc["libraries"].push_back(entry.to_json());
    }

    std::error_code ec;
    fs::create_directories(data_dir_, ec);

    return util::write_file_atomically(registry_file(), doc.dump(2) + '\n');
}

const LibraryEntry* LibraryRegistry::find(std::string_view id) const
{
    return util::find_by(entries_, id, &LibraryEntry::id);
}

const LibraryEntry* LibraryRegistry::active() const
{
    return active_id_.empty() ? nullptr : find(active_id_);
}

bool LibraryRegistry::id_taken(std::string_view id) const
{
    return find(id) != nullptr;
}

std::string LibraryRegistry::unique_id(std::string_view base) const
{
    std::string slug = slugify(base);
    if (slug.empty()) {
        slug = "library";
    }
    if (!id_taken(slug)) {
        return slug;
    }
    for (int n = 2; n < 10000; ++n) {
        std::string candidate = slug + "-" + std::to_string(n);
        if (!id_taken(candidate)) {
            return candidate;
        }
    }
    return slug + "-x";
}

bool LibraryRegistry::root_taken(const fs::path& root) const
{
    const auto target = normalized(root);
    return std::any_of(entries_.begin(), entries_.end(), [&](const LibraryEntry& e) {
        return normalized(e.root) == target;
    });
}

void LibraryRegistry::ensure_default()
{
    if (!load_error_.empty()) {
        return;
    }
    bool changed = false;
    if (entries_.empty()) {
        // A folder of its own, like every library the app creates, so nothing
        // else in the data directory is mistaken for this library's media.
        entries_.push_back(LibraryEntry{
            std::string{k_default_library_id},
            std::string{k_default_library_name},
            data_dir_ / std::string{k_libraries_dirname} / std::string{k_default_library_id},
        });
        changed = true;
    }
    if (find(active_id_) == nullptr) {
        active_id_ = entries_.front().id;
        changed = true;
    }
    if (changed) {
        (void)save();
    }
}

LibraryResult LibraryRegistry::set_active(std::string_view id)
{
    if (find(id) == nullptr) {
        return LibraryResult::failure("Unknown library");
    }
    if (active_id_ == id) {
        return LibraryResult::success(std::string{id});
    }
    active_id_ = std::string{id};
    if (!save()) {
        return LibraryResult::failure("Could not write the library registry");
    }
    return LibraryResult::success(active_id_);
}

LibraryResult LibraryRegistry::create(std::string_view name)
{
    const auto clean = util::trim(name);
    if (clean.empty()) {
        return LibraryResult::failure("Library name cannot be empty");
    }
    const auto id = unique_id(clean);
    const auto root = data_dir_ / std::string{k_libraries_dirname} / id;
    if (root_taken(root)) {
        return LibraryResult::failure("That folder is already a library");
    }
    return add_entry(LibraryEntry{id, clean, root}, "Could not create");
}

LibraryResult LibraryRegistry::attach(const fs::path& root, std::string_view name)
{
    if (root.empty()) {
        return LibraryResult::failure("Choose a folder for the library");
    }
    if (root_taken(root)) {
        return LibraryResult::failure("That folder is already a library");
    }

    auto clean = util::trim(name);
    if (clean.empty()) {
        clean = root.filename().string();
    }
    if (clean.empty()) {
        clean = "Library";
    }
    return add_entry(LibraryEntry{unique_id(clean), clean, normalized(root)}, "Could not use");
}

LibraryResult LibraryRegistry::add_entry(LibraryEntry entry, std::string_view could_not)
{
    // Refused before anything is made on disk, with the reason the list is.
    if (!load_error_.empty()) {
        return LibraryResult::failure(load_error_);
    }
    const fs::path root = entry.root;
    const fs::path media = media_root(root);
    std::error_code ec;
    const bool root_existed = fs::exists(root, ec);
    const bool media_existed = fs::exists(media, ec);
    fs::create_directories(media, ec);
    if (ec) {
        return LibraryResult::failure(std::string(could_not) + " " + root.string() + ": "
                                      + ec.message());
    }

    const std::string id = entry.id;
    entries_.push_back(std::move(entry));
    if (!save()) {
        entries_.pop_back();
        std::error_code cleanup;
        if (!media_existed) {
            fs::remove(media, cleanup);
        }
        if (!root_existed) {
            fs::remove(root, cleanup);
        }
        return LibraryResult::failure("Could not write the library registry");
    }
    return LibraryResult::success(id);
}

LibraryResult LibraryRegistry::rename(std::string_view id, std::string_view name)
{
    const auto clean = util::trim(name);
    if (clean.empty()) {
        return LibraryResult::failure("Library name cannot be empty");
    }
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const LibraryEntry& e) {
        return e.id == id;
    });
    if (it == entries_.end()) {
        return LibraryResult::failure("Unknown library");
    }
    if (it->name == clean) {
        return LibraryResult::success(it->id);
    }
    const auto previous = it->name;
    it->name = clean;
    if (!save()) {
        it->name = previous;
        return LibraryResult::failure("Could not write the library registry");
    }
    return LibraryResult::success(it->id);
}

LibraryResult LibraryRegistry::remove(std::string_view id)
{
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const LibraryEntry& e) {
        return e.id == id;
    });
    if (it == entries_.end()) {
        return LibraryResult::failure("Unknown library");
    }
    if (entries_.size() <= 1) {
        return LibraryResult::failure("The last library cannot be removed");
    }
    if (active_id_ == id) {
        return LibraryResult::failure("Switch to another library first");
    }

    // Files are deliberately left on disk - removing here only forgets the entry.
    const auto position = it - entries_.begin();
    auto backup = std::move(*it);
    entries_.erase(it);
    if (!save()) {
        // Back where it was: the list order is what the operator sees.
        entries_.insert(entries_.begin() + position, std::move(backup));
        return LibraryResult::failure("Could not write the library registry");
    }
    return LibraryResult::success(std::string{id});
}

bool LibraryRegistry::owns_folder(std::string_view id) const
{
    const auto* entry = find(id);
    if (entry == nullptr) {
        return false;
    }
    // Normalized, so a link in the libraries folder counts as where it points.
    const auto root = normalized(entry->root);
    if (root.parent_path() != normalized(data_dir_ / std::string{k_libraries_dirname})) {
        return false;
    }
    return std::none_of(entries_.begin(), entries_.end(), [&](const LibraryEntry& other) {
        if (other.id == entry->id) {
            return false;
        }
        const auto inside = normalized(other.root).lexically_relative(root);
        return !inside.empty() && *inside.begin() != "..";
    });
}

}  // namespace seventhslide::library
