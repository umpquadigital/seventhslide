#pragma once

// Library registry - named content collections (songs, freeform items, media,
// bibles, output themes), each rooted in its own directory.
//
// A *library* root holds the SQLite database plus the media/theme asset tree.
// The registry itself lives in the app data dir, outside every library, so it
// survives switching and can describe libraries that live anywhere on disk.
//
// Seam: plain C++20 + std::filesystem + nlohmann/json (no Qt).

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::library {

/// SQLite file inside a library root.
inline constexpr std::string_view k_database_filename = "seventhslide.db";
/// Media / theme asset root inside a library root (images, videos, thumbs).
inline constexpr std::string_view k_media_dirname = "media";
/// Registry file inside the app data dir (outside every library).
inline constexpr std::string_view k_registry_filename = "libraries.json";
/// Parent directory for libraries the app creates itself.
inline constexpr std::string_view k_libraries_dirname = "libraries";
/// Id of the library every install starts with, rooted at
/// ``<data dir>/libraries/default``.
inline constexpr std::string_view k_default_library_id = "default";
/// Name given to that library when the registry is first written.
inline constexpr std::string_view k_default_library_name = "Default Library";

/// ``<root>/sync``: the files the computers trade to keep the library in sync
/// (each computer's change files, the snapshots), as ``sync::FolderStore``
/// keeps them. The link copies each on its own; none is a library file.
inline constexpr std::string_view k_sync_dirname = "sync";
/// ``<root>/media/thumbs``: thumbnails, rebuilt from the media whenever missing.
inline constexpr std::string_view k_thumbs_dirname = "thumbs";
/// A file an import is still copying is named ``.import-<random>.part``, in
/// the folder it is going to (FileImporter).
inline constexpr std::string_view k_staging_prefix = ".import-";
inline constexpr std::string_view k_staging_suffix = ".part";

/// ``<root>/seventhslide.db`` - the one place this path is spelled out.
[[nodiscard]] std::filesystem::path database_file(const std::filesystem::path& root);
/// ``<root>/media`` - images, videos, theme backgrounds, thumbnails.
[[nodiscard]] std::filesystem::path media_root(const std::filesystem::path& root);
/// ``<root>/sync`` - what sync keeps on this computer (``k_sync_dirname``).
[[nodiscard]] std::filesystem::path sync_root(const std::filesystem::path& root);

/// True when ``relative``, a path inside a library's root, is not a file of
/// the library, so sync never copies it to another computer as one nor
/// removes it here. That is what is rebuilt or temporary: SQLite's working
/// files beside the database (``-wal``, ``-shm``, ``-journal``), thumbnails
/// (``media/thumbs``), a file an import is still copying, and the temp file
/// of a write (``*.tmp``); and the sync folder (``sync/``), whose files the
/// link trades each on its own.
///
/// The rest is the library. The database syncs by its changes, never as a
/// file; the media files sync as files.
[[nodiscard]] bool is_local_only(const std::filesystem::path& relative);

/// Filesystem-safe lowercase id fragment for a display name ("" when nothing survives).
[[nodiscard]] std::string slugify(std::string_view name);

struct LibraryEntry {
    std::string id;
    std::string name;
    std::filesystem::path root;

    [[nodiscard]] nlohmann::json to_json() const;
    /// ``std::nullopt`` when ``j`` is missing a usable id / root.
    [[nodiscard]] static std::optional<LibraryEntry> from_json(const nlohmann::json& j);

    [[nodiscard]] std::filesystem::path database_file() const;
    [[nodiscard]] std::filesystem::path media_root() const;
};

/// Outcome of a mutating registry call. Empty ``error`` means success.
struct LibraryResult {
    std::string error;
    std::string id;

    [[nodiscard]] bool ok() const noexcept { return error.empty(); }
    [[nodiscard]] static LibraryResult failure(std::string message)
    {
        return LibraryResult{std::move(message), {}};
    }
    [[nodiscard]] static LibraryResult success(std::string id)
    {
        return LibraryResult{{}, std::move(id)};
    }
};

/**
 * Persisted set of libraries plus which one is active.
 *
 * Mutating calls write ``libraries.json`` themselves so the on-disk registry can
 * never disagree with the in-memory list after a partially applied change.
 */
class LibraryRegistry {
public:
    explicit LibraryRegistry(std::filesystem::path data_dir);

    [[nodiscard]] const std::filesystem::path& data_dir() const noexcept { return data_dir_; }
    [[nodiscard]] std::filesystem::path registry_file() const;

    /// Read the registry. A missing file is a first run: the list is empty and
    /// ``ensure_default`` makes the default library. A file that is there but
    /// cannot be read (damaged, an entry without its id or folder, an id
    /// twice, or a version this build does not know) also leaves the list
    /// empty, and ``load_error`` says why; nothing writes over that file, and
    /// no library is created or attached, until a later ``load`` reads one.
    void load();
    /// Why the last ``load`` refused the registry file, for the operator; empty
    /// when it read it or there was none.
    [[nodiscard]] const std::string& load_error() const noexcept { return load_error_; }
    /// Write the registry atomically (temp file + rename). False on I/O failure,
    /// and while ``load_error`` is set.
    bool save() const;

    [[nodiscard]] const std::vector<LibraryEntry>& entries() const noexcept { return entries_; }
    [[nodiscard]] const LibraryEntry* find(std::string_view id) const;
    [[nodiscard]] const LibraryEntry* active() const;
    [[nodiscard]] const std::string& active_id() const noexcept { return active_id_; }

    /// Guarantee exactly one valid active entry, seeding the data-dir library when
    /// the registry is empty. Persists when it changed anything. Does nothing
    /// while ``load_error`` is set.
    void ensure_default();

    LibraryResult set_active(std::string_view id);
    /// Create (and register) ``<data_dir>/libraries/<slug>/``.
    LibraryResult create(std::string_view name);
    /// Register an existing directory; adopts a database already sitting there.
    LibraryResult attach(const std::filesystem::path& root, std::string_view name);
    LibraryResult rename(std::string_view id, std::string_view name);
    /// Forget a library. Never deletes files. Refuses the active / last entry.
    LibraryResult remove(std::string_view id);
    /// Whether library ``id``'s folder is the library's alone, for deleting
    /// with it: one the app made, directly in ``<data_dir>/libraries``, with no
    /// other library inside. Never a folder added from elsewhere, whatever else
    /// it holds.
    [[nodiscard]] bool owns_folder(std::string_view id) const;

private:
    [[nodiscard]] bool id_taken(std::string_view id) const;
    [[nodiscard]] std::string unique_id(std::string_view base) const;
    [[nodiscard]] bool root_taken(const std::filesystem::path& root) const;
    /// Make ``entry``'s folders and register it. A failure leaves nothing
    /// behind: the folders made just now go, never anything that was there.
    /// ``could_not`` starts the message when the folders cannot be made.
    LibraryResult add_entry(LibraryEntry entry, std::string_view could_not);

    std::filesystem::path data_dir_;
    std::vector<LibraryEntry> entries_;
    std::string active_id_;
    std::string load_error_;
};

} // namespace seventhslide::library
