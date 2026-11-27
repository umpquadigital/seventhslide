#pragma once

// A Freeform file: one item (.ssff) or several (.ssfb), with the pictures,
// videos and fonts they use, as one ZIP of stored entries. The two are the
// same format; an .ssff is a bundle of one. docs/FREEFORM_FILES.md is the
// reference this is held to.
//
//     manifest.json            what the file holds (Manifest), first
//     service.json             a service's rundown (Rundown), when it has one
//     items/0001.json ...      one Freeform document each
//     previews/0001.png ...    the first slide of each
//     media/<sha256>.<ext>     each picture and video, once
//     fonts/<sha256>.<ext>     each font face, once
//
// In the documents every file is named by its entry ("media/ab12….jpg"): a
// library's own names mean nothing in another library.
//
// A service's package is a ZIP holding a bundle with a rundown, and the
// service's pictures, videos and audio as files of their own, under Media/.
//
// The types here and their codecs are plain C++20 + nlohmann/json. Every
// read is of an untrusted file: it is checked whole, and refused in words
// for people.

#include "seventhslide/canvas/media_refs.hpp"
#include "seventhslide/geom/canvas.hpp"
#include "seventhslide/model/content_kind.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::exchange {

inline constexpr std::string_view k_bundle_format = "seventhslide.freeform-bundle";
/// The container's version. A reader reads this and every earlier one, and
/// refuses a later one.
inline constexpr int k_bundle_format_version = 1;

inline constexpr std::string_view k_single_extension = ".ssff";
inline constexpr std::string_view k_bundle_extension = ".ssfb";
/// A service's package: a ZIP holding a .ssfb and the service's own files.
inline constexpr std::string_view k_package_extension = ".zip";

inline constexpr std::string_view k_manifest_entry = "manifest.json";
inline constexpr std::string_view k_rundown_entry = "service.json";
inline constexpr std::string_view k_items_dir = "items/";
inline constexpr std::string_view k_previews_dir = "previews/";
inline constexpr std::string_view k_media_dir = "media/";
inline constexpr std::string_view k_fonts_dir = "fonts/";
/// Where a package keeps its loose files.
inline constexpr std::string_view k_package_media_dir = "Media/";

/// The most a manifest or a rundown may be.
inline constexpr std::uint64_t k_max_manifest_bytes = 4ull << 20;
/// The most a preview may be.
inline constexpr std::uint64_t k_max_preview_bytes = 4ull << 20;
/// The most items, media files, fonts, folders or rows one file may hold.
inline constexpr std::size_t k_max_listed = 20000;
/// How deep a manifest's or a document's JSON may nest.
inline constexpr int k_max_json_depth = 200;

/// An output a file has slides for, as the library it came from had it.
struct BundleOutput {
    std::string id;
    std::string name;
    geom::CanvasSize canvas;

    [[nodiscard]] bool operator==(const BundleOutput&) const = default;
};

/// A Freeform folder of the file's, under ``parent`` (empty: the top).
struct BundleFolder {
    std::string key;
    std::string name;
    std::string parent;

    [[nodiscard]] bool operator==(const BundleFolder&) const = default;
};

struct BundleItem {
    std::string entry;
    /// Empty when it has no preview.
    std::string preview;
    std::string name;
    /// A ``BundleFolder::key``; empty: the top.
    std::string folder;
    std::vector<std::string> tags;

    [[nodiscard]] bool operator==(const BundleItem&) const = default;
};

struct BundleMedia {
    std::string entry;
    canvas::MediaRefKind kind = canvas::MediaRefKind::Picture;
    /// What the library it came from called it.
    std::string title;
    std::uint64_t size = 0;
    std::string sha256;

    [[nodiscard]] bool operator==(const BundleMedia&) const = default;
};

struct BundleFont {
    std::string entry;
    std::string family;
    std::string style;
    bool bold = false;
    bool italic = false;
    bool variable = false;
    std::uint64_t size = 0;
    std::string sha256;

    [[nodiscard]] bool operator==(const BundleFont&) const = default;
};

struct Manifest {
    int format_version = k_bundle_format_version;
    /// The Freeform schema its documents are at (freeform::k_schema_version
    /// of the build that wrote it).
    int freeform_schema_version = 0;
    std::string made_by;
    /// UTC, ``yyyy-MM-ddTHH:mm:ssZ``.
    std::string made_at;
    /// The library_uid of the library it came from.
    std::string source_library;
    std::vector<BundleOutput> outputs;
    std::vector<BundleFolder> folders;
    std::vector<BundleItem> items;
    std::vector<BundleMedia> media;
    std::vector<BundleFont> fonts;
    /// What the items name that the library had no file for: pictures and
    /// videos by title, fonts by family.
    std::vector<std::string> media_not_included;
    std::vector<std::string> fonts_not_included;
    /// It holds ``service.json``.
    bool has_rundown = false;

    [[nodiscard]] bool operator==(const Manifest&) const = default;
};

[[nodiscard]] nlohmann::json to_json(const Manifest& manifest);
/// ``j`` as a manifest, or nullopt with ``refusal`` saying why it cannot be
/// read: not one, a later format, one that holds nothing, or anything out of
/// shape (entries not where they belong, folders that do not nest, more than
/// the limits). Its documents' Freeform schema is the opener's to check
/// (``open_freeform_file``).
[[nodiscard]] std::optional<Manifest> read_manifest(const nlohmann::json& j, std::string& refusal);

/// One row of a service's rundown.
struct RundownRow {
    model::ContentKind kind = model::ContentKind::Empty;
    std::string title;
    /// Song, Bible and Freeform: the bundle item it plays (``items/0003.json``).
    std::string item;
    /// Image, video and audio: its file in the package (``Media/01 Loop.mp4``).
    /// An image folder: each of its pictures.
    std::vector<std::string> files;
    /// What else the row holds, as its kind stores it: a video's or audio's
    /// ``autoplay`` and ``loop``; a web page's name, url, width, height and
    /// zoom; a live source's kind, name and url; an image folder's name.
    nlohmann::json data = nlohmann::json::object();
    /// The stack it plays on, as the library it came from had it; both empty
    /// for the default.
    std::string stack_id;
    std::string stack_name;
    /// The rows that go live with it, by place in the rundown.
    std::vector<std::size_t> also_play;

    [[nodiscard]] bool operator==(const RundownRow&) const = default;
};

struct Rundown {
    std::string name;
    std::vector<std::string> tags;
    /// When it is held (``model::Service::service_at``); empty for none.
    std::string service_at;
    nlohmann::json audio_fades = nlohmann::json::object();
    std::vector<RundownRow> rows;

    [[nodiscard]] bool operator==(const Rundown&) const = default;
};

[[nodiscard]] nlohmann::json to_json(const Rundown& rundown);
/// ``j`` as a rundown whose items are among ``items`` (``BundleItem::entry``),
/// or nullopt with ``refusal`` saying why. Each row's ``data`` keeps only the
/// keys its kind has, of the types they are (``row_data``); ``also_play``
/// keeps each other row once.
[[nodiscard]] std::optional<Rundown> read_rundown(const nlohmann::json& j,
                                                  const std::vector<BundleItem>& items,
                                                  std::string& refusal);

/// What of ``data`` a row of ``kind`` keeps: a video's or audio's
/// ``autoplay`` and ``loop`` (booleans); an image folder's ``folder_name``; a
/// web page's ``name``, ``url`` (strings), ``width``, ``height`` (integers)
/// and ``zoom``; a live source's ``kind``, ``name`` and ``url``. Nothing for
/// any other kind.
[[nodiscard]] nlohmann::json row_data(model::ContentKind kind, const nlohmann::json& data);

/// ``text`` parsed as JSON nested at most ``k_max_json_depth`` deep, or
/// nullopt.
[[nodiscard]] std::optional<nlohmann::json> parse_json_limited(std::string_view text);

/// ``items/0001.json``, ``previews/0001.png``: the ``n``th (from 1).
[[nodiscard]] std::string item_entry(std::size_t n);
[[nodiscard]] std::string preview_entry(std::size_t n);
/// ``media/<sha256><ext>``, ``fonts/<sha256><ext>``; ``ext`` lower-cased.
[[nodiscard]] std::string media_entry(std::string_view sha256, std::string_view ext);
[[nodiscard]] std::string font_entry(std::string_view sha256, std::string_view ext);

/// True when ``name`` is what a reader may open under ``dir`` (one of the
/// ``k_*_dir``): one plain name after it, of letters, digits, '.', '-' and
/// '_', not starting with '.'.
[[nodiscard]] bool is_entry_in(std::string_view name, std::string_view dir);
/// True when ``name`` is a package's loose file: under ``k_package_media_dir``,
/// relative, no part empty, "." or "..", no backslash or NUL.
[[nodiscard]] bool is_package_file(std::string_view name);

/// The file name an export suggests: ``<name>.ssff`` for one item, else
/// ``.ssfb``.
[[nodiscard]] std::string_view bundle_extension_for(std::size_t items, bool any_folder) noexcept;

}  // namespace seventhslide::exchange
