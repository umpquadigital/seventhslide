#pragma once

// A Freeform document's own pictures and videos: files of the document, kept
// apart from the library's images and videos, which never list them.
//
// Each owner has a folder of them under the media folder,
// ``freeform_media/<owner>/<name>``, and a document names each by its URL,
// ``/static/freeform_media/<owner>/<name>``. An item of the library owns the
// folder named by its uid; a service's copy of a document names the files of
// the item it was copied from, and a document that came with no item (a
// service imported on its own) has an owner of its own.
//
// A file here is never written over, as no library file is: a name means the
// same bytes on every computer. Taking another's file is a hard link where the
// filesystem has them, so a document's files cost nothing to copy. A file
// stays as long as a document names it, an item or a service's copy of one,
// and goes once none does (``remove_unnamed_own_media``).
//
// Seam: plain C++20, filesystem and nlohmann/json.

#include "seventhslide/canvas/media_refs.hpp"
#include "seventhslide/freeform/document.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace seventhslide::freeform {

/// One of a document's own files.
struct OwnMedia {
    std::string owner;
    std::string name;

    /// "<owner>/<name>": where it is under ``freeform_media``.
    [[nodiscard]] std::string place() const { return owner + "/" + name; }
    friend bool operator==(const OwnMedia&, const OwnMedia&) = default;
};

/// Whether ``part`` can be an owner or a file's name here: letters, digits,
/// '.', '_' and '-' only (what sync reads out of a URL), and not "." or "..".
[[nodiscard]] bool is_own_media_part(std::string_view part) noexcept;

/// The file ``ref`` names when it is a document's own; nullopt for anything
/// else (a library file, a theme's background).
[[nodiscard]] std::optional<OwnMedia> own_media_of(std::string_view ref);

/// The file at ``place`` ("<owner>/<name>"), or nullopt.
[[nodiscard]] std::optional<OwnMedia> own_media_at(std::string_view place);

/// ``/static/freeform_media/<owner>/<name>``.
[[nodiscard]] std::string own_media_url(const OwnMedia& file);

/// A name for a new file brought in as ``title``, with ``extension`` (".png"):
/// "<8 hex digits>-<the title, made safe>.png". Nothing but the file is ever
/// named so.
[[nodiscard]] std::string new_own_media_name(std::string_view title, std::string_view extension);

/// What people call own file ``name``: the title it was brought in as, with
/// its extension ("Sunrise over the lake.jpg").
[[nodiscard]] std::string own_media_title(std::string_view name);

/// An owner for a document that has no item of its own: 32 hex digits, as a
/// uid is.
[[nodiscard]] std::string new_own_media_owner();

/// ``owner``'s folder.
[[nodiscard]] std::filesystem::path own_media_dir(const std::filesystem::path& media_root,
                                                  std::string_view owner);
/// Where ``file`` is.
[[nodiscard]] std::filesystem::path own_media_path(const std::filesystem::path& media_root,
                                                   const OwnMedia& file);

/// Where the file ``ref`` names is on this computer: a document's own, a
/// library picture or video (by its name or its ``/static/`` URL) or a
/// theme's background. Empty for anything else, or for a file not here.
[[nodiscard]] std::filesystem::path media_ref_file(const std::filesystem::path& media_root,
                                                   std::string_view ref, canvas::MediaRefKind kind);

/// What a file taken into a document is called there, asked of the library
/// for one of its own (``ref``, a library file's name); empty for a name of
/// the file's own.
using MediaTitle = std::function<std::string(std::string_view ref, canvas::MediaRefKind kind)>;

/// What ``take_own_media`` did.
struct TakenMedia {
    /// Files now in the owner's folder.
    int taken = 0;
    /// Files that are not on this computer (still on their way, or gone),
    /// left named as they were.
    int missing = 0;
    /// Files that could not be put in the folder.
    int failed = 0;
};

/// Make every file ``doc`` names ``owner``'s own: a library picture or video,
/// a theme's background, or another document's file is linked into the
/// owner's folder (copied where the filesystem has no hard links) and named
/// there. A file another document owns keeps its name; any other takes a new
/// one from ``title_of``. The same file named twice is taken once.
TakenMedia take_own_media(Document& doc, std::string_view owner,
                          const std::filesystem::path& media_root, const MediaTitle& title_of = {});

/// The places ("<owner>/<name>") of the own files the stored document ``doc``
/// names, into ``out``.
void note_own_media(const nlohmann::json& doc, std::set<std::string, std::less<>>& out);

/// Remove each file of ``owner``'s folder that no document names (``named``,
/// places as ``note_own_media`` gives them), and the folder once it is empty.
/// A file an import is still copying is left alone. Returns how many went.
int remove_unnamed_own_media(const std::filesystem::path& media_root, std::string_view owner,
                             const std::set<std::string, std::less<>>& named);

}  // namespace seventhslide::freeform
