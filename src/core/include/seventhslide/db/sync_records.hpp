#pragma once

// Every table that syncs read as records, and written back: the document a
// record travels between computers as.
//
// A record is named by its key, as the outbox names it (``sync_outbox.hpp``):
// a uid, a setting's key, a file's name, a folder item's folder uid and
// filename joined by "/", an output's id. Its document is a JSON object of
// what the record holds, and nothing that only means something in this file:
//   - every column but the local ``id``, the ``uid`` and the key columns,
//     which the key already says;
//   - a reference (``k_sync_references``) as the uid of the record it points
//     at, or null; a reference inside JSON likewise, inside the JSON text;
//   - the rows that belong to it (``SyncAs::Part``) under their table's
//     name, as ``{"columns": [...], "rows": [[...], ...]}`` in the order they
//     were added (a Bible's verses: the order of its books is that order),
//     each row without its ``id`` or the reference to its owner.
// Values are as SQLite holds them: integers, reals, text and null. A JSON
// column stays the text it is, unless it holds a reference: then it is the
// same JSON with the reference turned, written out afresh.
//
// The documents are driven by the schema itself (each table's columns) and
// by ``sync_tables.hpp``, so a column added later syncs without a change
// here, and a reference added there is translated.
//
// Seam: Qt SQL via Database.

#include "seventhslide/db/database.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

/// The largest record that syncs, as its document's JSON: a long Bible, with
/// room to spare. A larger one is never sent; a Bible that large is not
/// imported.
inline constexpr std::size_t k_largest_record = 12 * 1024 * 1024;
/// What a file between computers holds beside a record's document, at most:
/// its table, key and versions, its media, a change file's label and signed
/// line. A file of the largest record has this much room for the rest.
inline constexpr std::size_t k_record_allowance = 1024 * 1024;

/// The tables whose records sync on their own, each after every table its
/// records refer to (a service item after services, songs and Bibles), so
/// records written in this order find what they point at.
[[nodiscard]] std::vector<std::string_view> record_tables_in_order();

/// The key of every record of ``table`` here: a library put online has
/// everything to send. Throws ``std::invalid_argument`` for a table that does
/// not sync on its own (a part, or this computer's bookkeeping).
[[nodiscard]] std::vector<std::string> record_keys(Database& db, std::string_view table);

/// Whether ``table`` holds any record (one ``record_keys`` would list),
/// without listing them.
[[nodiscard]] bool has_records(Database& db, std::string_view table);

/// ``table``'s record ``key`` as its document, or nullopt when there is none.
/// A reference to a record that is not here reads as null (an object keyed by
/// ids leaves the entry out).
[[nodiscard]] std::optional<nlohmann::json> read_record(Database& db, std::string_view table,
                                                         std::string_view key);

struct RecordWrite {
    /// False when a reference the record cannot be without (its service, its
    /// folder) names a record that is not here; nothing was written.
    bool written = false;
    /// References to records that are not here, written as null (or left
    /// out, for an object keyed by ids): to try again once those arrive.
    std::vector<std::string> unresolved;
};

/// Write ``document`` as ``table``'s record ``key``, adding it or replacing
/// what it holds, all at once. A field the document leaves out keeps what the
/// record had (its default, for a new one); a Bible without ``verses`` keeps
/// its verses. A record added takes ``id_here`` when given and free: a record
/// that comes back after a delete takes the id it had, so what still points
/// at it by that id (inside JSON, where no foreign key reaches) finds it.
/// Throws ``std::invalid_argument`` for a field the table does not have, and
/// ``std::runtime_error`` when the database refuses the write.
RecordWrite write_record(Database& db, std::string_view table, std::string_view key,
                         const nlohmann::json& document,
                         std::optional<std::int64_t> id_here = std::nullopt);

/// The id here of ``table``'s record ``key``, for a table that syncs by uid;
/// nullopt for one that is not here, or a table of another kind.
[[nodiscard]] std::optional<std::int64_t> record_id(Database& db, std::string_view table,
                                                    std::string_view key);

/// Whether ``table``'s record ``key`` is here, without reading it.
[[nodiscard]] bool record_exists(Database& db, std::string_view table, std::string_view key);

/// Delete ``table``'s record ``key``, with what belongs to it. Returns false
/// when there was none.
bool delete_record(Database& db, std::string_view table, std::string_view key);

/// The keys of the records ``table``'s record ``key``, holding ``document``
/// (as ``read_record`` reads it, or its shared form), names: every uid in it
/// (32 lower-case hex digits, wherever they are, JSON held as text and an
/// object's member names too), the media files it names where a record may
/// name one (``k_sync_file_names``: a string that is the file's name or a
/// path or URL ending in it), and the parts of a key that joins two (a
/// folder's entry: the folder's uid and the file's name). Naming a record
/// counts as using it.
[[nodiscard]] std::set<std::string> names_in(std::string_view table, std::string_view key,
                                             const nlohmann::json& document);

}  // namespace seventhslide::db
