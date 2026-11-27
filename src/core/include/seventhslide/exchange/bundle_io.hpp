#pragma once

// Writing a Freeform file or a service's package, and opening one to import.
// File IO through util's ZIP writer and reader; no Qt, no database: the
// caller reads the library and hands in what goes, and takes in what comes.
// Plain C++20 + nlohmann/json.

#include "seventhslide/db/font_repository.hpp"
#include "seventhslide/exchange/adapt.hpp"
#include "seventhslide/exchange/bundle.hpp"
#include "seventhslide/freeform/document.hpp"
#include "seventhslide/util/zip_reader.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace seventhslide::exchange {

/// The most a file's documents may come to together, read at once.
inline constexpr std::uint64_t k_max_documents_bytes = 128ull << 20;

/// Hears bytes done of ``total``; false stops the work.
using Progress = std::function<bool(std::uint64_t done, std::uint64_t total)>;

struct ItemToPack {
    std::string name;
    /// A ``BundleFolder::key`` of the source's; empty: the top.
    std::string folder;
    std::vector<std::string> tags;
    /// Its decks already those that go (``keep_decks_of``), its files named
    /// as the library names them.
    freeform::Document document;
    /// Its first slide, as PNG; empty for none.
    std::string preview_png;
};

/// A file an item names, as the library has it.
struct FileToPack {
    /// Empty when it is not on this computer.
    std::filesystem::path path;
    std::string title;
    canvas::MediaRefKind kind = canvas::MediaRefKind::Picture;
};

struct FontToPack {
    std::filesystem::path path;
    db::LibraryFont font;
};

struct BundleSource {
    std::string made_by;
    std::string made_at;
    std::string source_library;
    std::vector<BundleOutput> outputs;
    std::vector<BundleFolder> folders;
    /// Written as ``item_entry(1)``, ``item_entry(2)``, ... in this order, which
    /// is how a rundown's rows name them.
    std::vector<ItemToPack> items;
    /// Every reference the items make, as it stands in them.
    std::map<std::string, FileToPack, std::less<>> media;
    std::vector<FontToPack> fonts;
    /// Families the items name that the library has no face of.
    std::vector<std::string> fonts_not_included;
    std::optional<Rundown> rundown;
};

struct WriteOutcome {
    bool ok = false;
    bool stopped = false;
    /// Why it failed, for people.
    std::string error;
    /// Titles of the pictures and videos that were not on this computer.
    std::vector<std::string> media_not_included;
    /// Titles of those that were, but could not be read.
    std::vector<std::string> media_unreadable;
    std::vector<std::string> fonts_not_included;
    /// Faces ("Lato Bold") whose files could not be read.
    std::vector<std::string> font_files_unreadable;
    std::size_t media_files = 0;
    std::size_t font_files = 0;
};

/// Write ``source`` as a Freeform file at ``file`` (replaced). On anything but
/// success ``file`` is removed.
[[nodiscard]] WriteOutcome write_bundle(const BundleSource& source, const std::filesystem::path& file,
                                        const Progress& progress);

/// A package's loose file: its name in the package (``Media/…``) and where it is.
struct LooseFile {
    std::string name;
    std::filesystem::path path;
};

/// Write a service's package at ``file`` (replaced): the Freeform file
/// ``bundle`` as ``bundle_name`` (``Sunday.ssfb``), then ``loose``. On anything
/// but success ``file`` is removed.
[[nodiscard]] WriteOutcome write_package(const std::filesystem::path& file, std::string_view bundle_name,
                                         const std::filesystem::path& bundle,
                                         const std::vector<LooseFile>& loose, const Progress& progress);

/// A Freeform file opened to import, read and checked whole.
struct OpenedFile {
    util::ZipReader bundle;
    /// The package the bundle was in, when it was in one.
    std::optional<util::ZipReader> package;
    Manifest manifest;
    std::optional<Rundown> rundown;

    struct Item {
        /// As the file holds it, files named by entry; nullopt when refused.
        std::optional<freeform::Document> document;
        std::string refusal;
    };
    /// By place in ``manifest.items``.
    std::vector<Item> items;
};

/// ``path`` opened: a Freeform file, or a package holding one. Nullopt with
/// ``refusal`` saying why, for people.
[[nodiscard]] std::optional<OpenedFile> open_freeform_file(const std::filesystem::path& path,
                                                           std::string& refusal);

/// Item ``n``'s preview PNG, or empty.
[[nodiscard]] std::string read_preview(const OpenedFile& file, std::size_t n);

/// The faces to take in from ``file`` for ``families`` (case aside), and
/// those left out because ``library`` has a face in that slot (family, case
/// aside, bold and italic), each as "<family> <style>" in ``kept``.
[[nodiscard]] std::vector<std::size_t> fonts_to_take(const Manifest& file,
                                                     const std::set<std::string>& families,
                                                     const std::vector<db::LibraryFont>& library,
                                                     std::vector<std::string>& kept);

}  // namespace seventhslide::exchange
