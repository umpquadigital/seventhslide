#pragma once

// How each table of a library will sync between computers, and every place a
// row points at another row: what a sync engine needs to know about the
// schema, kept beside it so it cannot fall behind.
//
// A row's ``id`` (its ``rowid``) only means something in one library file:
// two computers each adding a song both make song 57. So a row that syncs on
// its own is named by something that means the same on every computer:
//   - a ``uid`` (``SyncAs::Record``): a song, a service, a folder;
//   - columns that are already the same everywhere (``SyncAs::Key``): a
//     setting's key, an image's opaque filename, a folder plus a filename;
//   - an id inside its JSON (``SyncAs::JsonId``): an output.
// The rest go with the row they belong to (``SyncAs::Part``), or not at all:
// this computer's own bookkeeping for syncing (``SyncAs::Local``).
//
// Every write to a table that syncs marks its record in the outbox
// (``sync_outbox.hpp``).
//
// The local integer ids stay as they are, in the columns and in the JSON
// listed in ``k_sync_references``. A sync engine writes the target's uid in
// their place when a row leaves this computer, and turns it back into this
// file's id when a row arrives.
//
// Tests hold the schema to these lists: every table is in ``k_sync_tables``,
// every ``Record`` table has its uid, every table that syncs marks the outbox,
// and every foreign key is in ``k_sync_references``. A new table or reference
// therefore has to say how it syncs.
//
// The same Bible imported on two computers is two records, as anything
// imported twice is: imports are never merged. A known gap for sync to
// settle: a library folder copied by hand keeps its ``library_uid``, so the
// copy and the original would sync as one library.
//
// Seam: plain C++20 (no Qt).

#include "seventhslide/model/static_media.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string_view>

namespace seventhslide::db {

enum class SyncAs : std::uint8_t {
    /// Row by row, each named by its ``uid``.
    Record,
    /// Row by row, named by ``key`` columns that mean the same everywhere.
    Key,
    /// Row by row, named by an id inside its JSON. Every save rewrites the
    /// whole table, so it is marked as a whole (``marked_whole``).
    JsonId,
    /// With the row it belongs to, never on its own.
    Part,
    /// Not at all: this computer's own bookkeeping for syncing.
    Local,
};

struct SyncTable {
    std::string_view table;
    SyncAs as;
    /// What names a row: the key columns (``Key``), the JSON field
    /// (``JsonId``), the table it belongs to (``Part``); empty otherwise.
    std::string_view key = {};
};

inline constexpr std::array k_sync_tables = {
    SyncTable{"songs", SyncAs::Record},
    SyncTable{"services", SyncAs::Record},
    SyncTable{"service_items", SyncAs::Record},
    SyncTable{"freeform_folders", SyncAs::Record},
    SyncTable{"freeform_items", SyncAs::Record},
    SyncTable{"image_folders", SyncAs::Record},
    SyncTable{"video_folders", SyncAs::Record},
    SyncTable{"audio_folders", SyncAs::Record},
    SyncTable{"bibles", SyncAs::Record},
    SyncTable{"style_profiles", SyncAs::Record},
    SyncTable{"web_folders", SyncAs::Record},
    SyncTable{"web_pages", SyncAs::Record},
    // The library's own facts; its uid is made once and copied, never edited.
    SyncTable{"library_meta", SyncAs::Key, "key"},
    SyncTable{"app_settings", SyncAs::Key, "key"},
    // Imports are stored under opaque, random names, so a filename names the
    // same file on every computer.
    SyncTable{"image_files", SyncAs::Key, "filename"},
    SyncTable{"video_files", SyncAs::Key, "filename"},
    SyncTable{"audio_files", SyncAs::Key, "filename"},
    // Named by their content (FontRepository), so the same font added on two
    // computers is one record.
    SyncTable{"font_files", SyncAs::Key, "filename"},
    SyncTable{"image_folder_items", SyncAs::Key, "folder_id, filename"},
    SyncTable{"video_folder_items", SyncAs::Key, "folder_id, filename"},
    SyncTable{"audio_folder_items", SyncAs::Key, "folder_id, filename"},
    // ConfigRepository::save_outputs rewrites every row, so the rowid means
    // nothing; the output's id is in its JSON.
    SyncTable{"outputs", SyncAs::JsonId, "data.id"},
    SyncTable{"verses", SyncAs::Part, "bibles"},
    SyncTable{"sync_outbox", SyncAs::Local},
    SyncTable{"sync_shared", SyncAs::Local},
    SyncTable{"sync_applied", SyncAs::Local},
    SyncTable{"sync_state", SyncAs::Local},
    SyncTable{"sync_held", SyncAs::Local},
    SyncTable{"sync_media", SyncAs::Local},
};

/// Where a reference keeps the local id it holds.
enum class RefIn : std::uint8_t {
    /// A column of its own: the field is the column.
    Column,
    /// A value in a JSON column: "data.bible_id" is the ``bible_id`` member
    /// of the object in ``data``.
    JsonValue,
    /// The member names of an object in a JSON column: "data.songs" is an
    /// object keyed by song id.
    JsonKeys,
    /// A list of ids in a JSON column, or a single one: "data.also_play".
    JsonList,
    /// The JSON value of one setting (``app_settings``): the field is the
    /// setting's key.
    Setting,
};

/// A row of ``table`` naming a row of ``target`` by its local id.
struct SyncReference {
    std::string_view table;
    /// The column, or for a reference inside JSON the column and the member
    /// in it ("data.bible_id"), or the setting's key.
    std::string_view field;
    std::string_view target;
    /// Anywhere but a column of its own: no foreign key guards it, and only
    /// the rows the note names hold it.
    RefIn in = RefIn::Column;
    std::string_view note = {};
};

inline constexpr std::array k_sync_references = {
    // Foreign keys.
    SyncReference{"service_items", "service_id", "services"},
    SyncReference{"service_items", "song_id", "songs"},
    SyncReference{"freeform_folders", "parent_id", "freeform_folders"},
    SyncReference{"freeform_items", "folder_id", "freeform_folders"},
    SyncReference{"web_folders", "parent_id", "web_folders"},
    SyncReference{"web_pages", "folder_id", "web_folders"},
    SyncReference{"image_folders", "parent_id", "image_folders"},
    SyncReference{"image_folder_items", "folder_id", "image_folders"},
    SyncReference{"video_folders", "parent_id", "video_folders"},
    SyncReference{"video_folder_items", "folder_id", "video_folders"},
    SyncReference{"audio_folders", "parent_id", "audio_folders"},
    SyncReference{"audio_folder_items", "folder_id", "audio_folders"},
    SyncReference{"verses", "bible_id", "bibles"},
    // Inside JSON.
    SyncReference{"service_items", "data.bible_id", "bibles", RefIn::JsonValue,
                  "Bible rows: the passage's translation (model::BiblePassage)"},
    SyncReference{"service_items", "data.folder_id", "image_folders", RefIn::JsonValue,
                  "image folder rows: the folder they show (MediaController)"},
    SyncReference{"service_items", "data.page_id", "web_pages", RefIn::JsonValue,
                  "web page rows: the library page they came from (model::web_item_data)"},
    SyncReference{"service_items", "data.also_play", "service_items", RefIn::JsonList,
                  "the rows that go live with it (model::service_item_also_play)"},
    SyncReference{"style_profiles", "data.songs", "songs", RefIn::JsonKeys,
                  "each song's theme map, keyed by song id (style_profiles.cpp)"},
    SyncReference{"style_profiles", "data.song_verse_orders", "songs", RefIn::JsonKeys,
                  "each song's verse order, keyed by song id (style_profiles.cpp)"},
    SyncReference{"app_settings", "active_profile_id", "style_profiles", RefIn::Setting,
                  "the value of that setting (k_active_profile_setting)"},
};

/// Where a record names the files it owns.
enum class FileIn : std::uint8_t {
    /// The record is a file's: its key is the file's name (a media registry
    /// row, ``image_files``).
    Key,
    /// Its JSON names files by URL, ``/static/<folder>/<name>``: an output's
    /// themes, their backgrounds.
    Url,
};

/// Records of ``table`` owning files in ``folder`` under the library's media
/// folder (``library::media_root``). A file in the library is never written
/// over: changed content is a new file under a new name, so a name means the
/// same bytes on every computer, and sync moves files by name. A record's
/// files go wherever it goes: sent with it, fetched before it is applied,
/// removed with it.
///
/// Tests hold the library folder to this list: every file in it, but the
/// database and what ``library::is_local_only`` names, is owned by a record
/// here. So a new kind of file has to say which record owns it.
struct SyncFile {
    std::string_view table;
    FileIn in;
    /// The folder under the media folder; also the ``/static/`` segment.
    std::string_view folder;
    /// Its files are a folder further down, one for each owner:
    /// "<folder>/<owner>/<name>" (``FileIn::Url`` only).
    bool per_owner = false;
    /// Its records go however large their files are: the size limit never
    /// keeps one here (an output, which holds every theme).
    bool never_held = false;
};

inline constexpr std::array k_sync_files = {
    // The media registries.
    SyncFile{"image_files", FileIn::Key, model::k_images_subdir},
    SyncFile{"video_files", FileIn::Key, model::k_videos_subdir},
    SyncFile{"audio_files", FileIn::Key, model::k_audio_subdir},
    // The library's fonts. Text names a font by its family, not by this file,
    // so a font's file is never in ``k_sync_file_names``.
    SyncFile{"font_files", FileIn::Key, model::k_fonts_subdir},
    // Theme backgrounds, each owned by the one theme that names it.
    SyncFile{"outputs", FileIn::Url, model::k_theme_backgrounds_subdir, false, true},
    // A Freeform document's own pictures and videos (freeform/own_media.hpp):
    // owned by every document that names one, the item and the services'
    // copies of it, so a file goes only once none of them does.
    SyncFile{"freeform_items", FileIn::Url, model::k_freeform_media_subdir, true},
    SyncFile{"service_items", FileIn::Url, model::k_freeform_media_subdir, true},
};

/// Where a record may name a media file of the library (a ``FileIn::Key``
/// record of ``k_sync_files``) by the file's name: anywhere in this column's
/// value, as the name or as a path or URL ending in it. A folder's entry
/// names its file by its key. A name anywhere else, in a song's title or
/// lyrics, is only text. Naming a file counts as using it: a delete of the
/// file made elsewhere does not go through here (``sync_engine.hpp``).
///
/// Tests hold a library with every kind of row naming a file to this list:
/// every column a file's name is found in is one of these.
struct SyncFileName {
    std::string_view table;
    std::string_view column;
};

inline constexpr std::array k_sync_file_names = {
    // An image, video or sound row's file, an image folder row's images, a
    // Freeform row's document.
    SyncFileName{"service_items", "data"},
    // Its image objects and image backgrounds.
    SyncFileName{"freeform_items", "document"},
    // Its themes' backgrounds.
    SyncFileName{"outputs", "data"},
};

/// A field holding a list of words, as JSON text. Changed on two computers
/// at once, it keeps both changes (the words each added, without those each
/// removed) rather than either.
struct SyncWordList {
    std::string_view table;
    std::string_view field;
};

inline constexpr std::array k_sync_word_lists = {
    SyncWordList{"services", "tags"},
    SyncWordList{"freeform_items", "tags"},
    SyncWordList{"songs", "topics"},
};

/// A table whose records keep both versions when changed differently on two
/// computers: the one that wins keeps the record, the other becomes a copy
/// beside it, named in ``name`` as "<name> (from <computer>)". In every
/// other table the later change wins.
struct SyncKeepBoth {
    std::string_view table;
    std::string_view name;
};

inline constexpr std::array k_sync_keep_both = {
    SyncKeepBoth{"songs", "title"},
    SyncKeepBoth{"freeform_items", "name"},
};

// What the lists say of one table, asked by its name. Code that needs to
// know how a table syncs asks here rather than searching the lists itself.

/// ``table``'s entry in ``k_sync_tables``; null for a table not there.
[[nodiscard]] constexpr const SyncTable* sync_table(std::string_view table)
{
    for (const auto& t : k_sync_tables) {
        if (t.table == table) {
            return &t;
        }
    }
    return nullptr;
}

/// Whether ``table``'s rows sync as ``as``.
[[nodiscard]] constexpr bool syncs_as(std::string_view table, SyncAs as)
{
    const auto* t = sync_table(table);
    return t != nullptr && t->as == as;
}

/// Whether ``table``'s rows sync on their own, as records (``Record``,
/// ``Key`` or ``JsonId``), rather than with another's or not at all.
[[nodiscard]] constexpr bool syncs_as_records(std::string_view table)
{
    return syncs_as(table, SyncAs::Record) || syncs_as(table, SyncAs::Key) || syncs_as(table, SyncAs::JsonId);
}

/// Whether ``table`` syncs by uid (``SyncAs::Record``).
[[nodiscard]] constexpr bool syncs_by_uid(std::string_view table)
{
    return syncs_as(table, SyncAs::Record);
}

/// Whether ``table`` is marked as a whole (``SyncAs::JsonId``: the outputs),
/// its mark keyed "".
[[nodiscard]] constexpr bool marked_whole(std::string_view table)
{
    return syncs_as(table, SyncAs::JsonId);
}

/// The reference a column of ``table``'s own holds (``RefIn::Column``); null
/// for a column that holds none.
[[nodiscard]] constexpr const SyncReference* column_reference(std::string_view table, std::string_view column)
{
    for (const auto& r : k_sync_references) {
        if (r.in == RefIn::Column && r.table == table && r.field == column) {
            return &r;
        }
    }
    return nullptr;
}

/// The table whose records ``table``'s rows belong to: a part's owner, or
/// the table itself. A part's references are its owner's.
[[nodiscard]] constexpr std::string_view owner_of(std::string_view table)
{
    const auto* t = sync_table(table);
    return t != nullptr && t->as == SyncAs::Part ? t->key : table;
}

/// Whether a row of ``table`` may point at a row of ``target``
/// (``k_sync_references``).
[[nodiscard]] constexpr bool points_at(std::string_view table, std::string_view target)
{
    for (const auto& r : k_sync_references) {
        if (r.table == table && r.target == target) {
            return true;
        }
    }
    return false;
}

/// Whether a record of ``table`` may name another of it (a folder its parent).
[[nodiscard]] constexpr bool names_its_own(std::string_view table)
{
    return points_at(table, table);
}

/// A part table (``SyncAs::Part``) with its owner and the column naming it.
struct SyncPart {
    std::string_view table;
    std::string_view owner;
    std::string_view owner_column;
};

/// Every part table. Each has a column referring to its owner; one without
/// does not compile.
inline constexpr auto k_sync_parts = [] {
    constexpr auto count = [] {
        std::size_t n = 0;
        for (const auto& t : k_sync_tables) {
            n += t.as == SyncAs::Part ? 1 : 0;
        }
        return n;
    }();
    std::array<SyncPart, count> out{};
    std::size_t at = 0;
    for (const auto& t : k_sync_tables) {
        if (t.as != SyncAs::Part) {
            continue;
        }
        const SyncReference* owner = nullptr;
        for (const auto& r : k_sync_references) {
            if (r.in == RefIn::Column && r.table == t.table && r.target == t.key) {
                owner = &r;
            }
        }
        if (owner == nullptr) {
            throw "a part table has no column referring to its owner";
        }
        out[at++] = {t.table, t.key, owner->field};
    }
    return out;
}();

/// The part tables of ``owner``: a Bible's verses.
[[nodiscard]] inline auto parts_of(std::string_view owner)
{
    return k_sync_parts | std::views::filter([owner](const SyncPart& p) { return p.owner == owner; });
}

/// Whether ``owner`` has part tables.
[[nodiscard]] constexpr bool has_parts(std::string_view owner)
{
    for (const auto& p : k_sync_parts) {
        if (p.owner == owner) {
            return true;
        }
    }
    return false;
}

/// Whether ``table``'s records are files, each named by its file's name
/// (``FileIn::Key``).
[[nodiscard]] constexpr bool is_file_table(std::string_view table)
{
    for (const auto& f : k_sync_files) {
        if (f.table == table && f.in == FileIn::Key) {
            return true;
        }
    }
    return false;
}

/// Whether ``table``'s records own media files (``k_sync_files``): by their
/// name, or named in their JSON.
[[nodiscard]] constexpr bool owns_files(std::string_view table)
{
    for (const auto& f : k_sync_files) {
        if (f.table == table) {
            return true;
        }
    }
    return false;
}

/// Whether the size limit lets ``table``'s records go whatever files they
/// own (``SyncFile::never_held``).
[[nodiscard]] constexpr bool never_held_back(std::string_view table)
{
    for (const auto& f : k_sync_files) {
        if (f.table == table && f.never_held) {
            return true;
        }
    }
    return false;
}

/// Whether ``table``'s ``column`` may name a media file (``k_sync_file_names``).
[[nodiscard]] constexpr bool names_files(std::string_view table, std::string_view column)
{
    for (const auto& f : k_sync_file_names) {
        if (f.table == table && f.column == column) {
            return true;
        }
    }
    return false;
}

/// Whether ``table``'s ``field`` is a list of words (``k_sync_word_lists``).
[[nodiscard]] constexpr bool is_word_list(std::string_view table, std::string_view field)
{
    for (const auto& w : k_sync_word_lists) {
        if (w.table == table && w.field == field) {
            return true;
        }
    }
    return false;
}

/// The field a copy's name goes in, for a table whose records keep both
/// versions (``k_sync_keep_both``); empty for the others.
[[nodiscard]] constexpr std::string_view copy_name_field(std::string_view table)
{
    for (const auto& k : k_sync_keep_both) {
        if (k.table == table) {
            return k.name;
        }
    }
    return {};
}

/// A file's own name, safe to use under a folder: no folders in it, nothing
/// that leaves the folder.
[[nodiscard]] inline bool is_media_file_name(std::string_view name)
{
    return !name.empty() && name != "." && name != ".."
           && name.find_first_of(std::string_view("/\\\0", 3)) == std::string_view::npos;
}

/// Whether ``path`` names a media file as sync does, "<folder>/<name>": the
/// folder of one of ``k_sync_files``, and a file's own name under it; for a
/// folder with one for each owner, "<folder>/<owner>/<name>".
[[nodiscard]] inline bool is_sync_media_path(std::string_view path)
{
    const auto slash = path.find('/');
    if (slash == std::string_view::npos) {
        return false;
    }
    const auto folder = path.substr(0, slash);
    const auto rest = path.substr(slash + 1);
    for (const auto& f : k_sync_files) {
        if (f.folder != folder) {
            continue;
        }
        if (!f.per_owner) {
            return is_media_file_name(rest);
        }
        const auto inner = rest.find('/');
        return inner != std::string_view::npos && is_media_file_name(rest.substr(0, inner))
               && is_media_file_name(rest.substr(inner + 1));
    }
    return false;
}

}  // namespace seventhslide::db
