#pragma once

// The kinds of media file the library keeps, and what differs between them.
//
// Each kind has its own folder under the library's media folder, its own
// registry (``<kind>_files``: a row per file with its title and a hidden flag)
// and its own pair of folder tables (``<kind>_folders``,
// ``<kind>_folder_items``). The registry is the library: a file is in it
// because it was imported, never because it sits in the folder. An import
// stores the file under an opaque name that never changes, so renaming
// changes only the title, and a name means the same file on every computer
// the library is shared with.
//
// Code that treats the kinds differently asks a question of this header
// (``hidden_when_in_use``) rather than testing for one kind, so a new kind is
// a new row here.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace seventhslide::db {

enum class MediaKind : std::uint8_t { Image, Video, Audio };

/// Every kind, in enum order: ``k_media_kinds[media_index(k)] == k``.
inline constexpr std::array<MediaKind, 3> k_media_kinds = {MediaKind::Image, MediaKind::Video,
                                                           MediaKind::Audio};

/// ``kind``'s slot in an array sized ``k_media_kinds.size()``.
[[nodiscard]] constexpr std::size_t media_index(MediaKind kind) noexcept
{
    return static_cast<std::size_t>(kind);
}

static_assert(
    [] {
        for (std::size_t i = 0; i < k_media_kinds.size(); ++i) {
            if (media_index(k_media_kinds[i]) != i) {
                return false;
            }
        }
        return true;
    }(),
    "k_media_kinds must list every MediaKind in enum order");

/// What deleting a file still in use does: an image a service or a Freeform
/// still shows is hidden from the library and kept (true); a video or audio
/// file a service item plays refuses the delete (false).
[[nodiscard]] constexpr bool hidden_when_in_use(MediaKind kind) noexcept
{
    return kind == MediaKind::Image;
}

/// ``kind``'s tables: its registry and its pair of folder tables.
struct MediaTables {
    const char* files;
    const char* folders;
    const char* items;
};
[[nodiscard]] MediaTables media_tables(MediaKind kind);

/// The folder under the library's media folder that holds ``kind``'s files,
/// which is also the ``/static/`` segment they are served under.
[[nodiscard]] std::string_view media_subdir(MediaKind kind) noexcept;

/// The extensions ``kind`` imports and lists: lower case, with the dot.
[[nodiscard]] std::span<const std::string_view> media_extensions(MediaKind kind) noexcept;

/// True when ``name`` ends in one of ``kind``'s extensions, in any case.
[[nodiscard]] bool is_media_filename(MediaKind kind, std::string_view name);

/// The largest file of ``kind`` the library takes in.
[[nodiscard]] constexpr std::uintmax_t max_import_bytes(MediaKind kind) noexcept
{
    constexpr std::uintmax_t mib = 1024ull * 1024ull;
    switch (kind) {
    case MediaKind::Image:
        return 50ull * mib;
    case MediaKind::Video:
        return 8ull * 1024ull * mib;
    case MediaKind::Audio:
        return 2ull * 1024ull * mib;
    }
    return 0;
}

/// True when ``name`` ends in an extension of any kind.
[[nodiscard]] bool is_any_media_filename(std::string_view name);

}  // namespace seventhslide::db
