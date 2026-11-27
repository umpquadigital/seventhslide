#pragma once

// A change file: what one computer sends in one go, the changes it made since
// it last sent. Each computer numbers its own files 1, 2, 3, ... and only ever
// writes its own, so no file is written by two computers.
//
//   {
//     "format": 2,
//     "schema": 11,
//     "library": "<library uid>",
//     "machine": "<machine id>",
//     "computer": "Church PC",
//     "time": "2026-10-23T20:15:00Z",
//     "sequence": 3,
//     "counter": 17,
//     "seen": {"<machine id>": 5, ...},
//     "media": [{"path": "images/<name>", "size": 1234, "root": "<hex>"}],
//     "changes": [
//       {"table": "songs", "key": "<uid>",
//        "base": {"title": [12, "<machine id>"], "key": null},
//        "document": {...}},
//       {"table": "services", "key": "<uid>",
//        "base": {"tags": [12, "<machine id>"]},
//        "before": {"tags": "[\"Easter\"]"}, "document": {...}},
//       {"table": "songs", "key": "<uid>", "deleted": true,
//        "base": {"title": [12, "<machine id>"], ...},
//        "referrers": [{"table": "service_items", "key": "<uid>",
//                       "versions": {"song_id": [9, "<machine id>"], ...}}]}
//     ]
//   }
//
// Every change in a file has the file's version, ``[counter, machine]``.
// ``seen`` is the last file this computer had applied from each other one when
// it sent this file: a file is applied only after those, so a change is never
// applied before one it was made on top of. ``base`` names the fields a change
// changed, each with the version it had when the change was made (null when
// it had none: a new record); for a delete it holds every field's. A change
// carries its record's whole document, so a computer that does not have the
// record can add it. ``media`` declares the media files the changes bring
// (each file a record in the file owns that it did not before): their size
// and fingerprint, so the computer fetching one from anyone can check it, and
// the file need never be opened to count the library's size. ``computer`` and
// ``time`` are for people (a record's history); nothing is ordered by them. ``schema`` is the schema of the
// library the file was made from: a file of a later schema, or a later
// format, came from a newer SeventhSlide and is refused, saying so; one of an
// earlier schema is taken, the fields it lacks keeping what they have.
//
// Seam: nlohmann/json; versions are db's (``sync_bookkeeping.hpp``).

#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_bookkeeping.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace seventhslide::sync {

/// The change file format this build writes and reads. A file of a later one
/// came from a newer SeventhSlide.
inline constexpr int k_change_format = 2;

/// A record that pointed at one deleted, as the deleting computer last saw
/// it: the version of each of its fields.
struct Referrer {
    std::string table;
    std::string key;
    std::map<std::string, db::SyncVersion> versions;

    friend bool operator==(const Referrer&, const Referrer&) = default;
};

struct Change {
    std::string table;
    std::string key;
    bool deleted = false;
    /// The fields changed (for a delete, every field), each with the version
    /// it had before; nullopt when it had none.
    std::map<std::string, std::optional<db::SyncVersion>> base;
    /// The record as it is after the change; null for a delete.
    nlohmann::json document;
    /// For a field that holds a list of words (a service's tags): what it
    /// held before the change, so a computer that changed it too can keep
    /// both changes (the words each added, without those each removed).
    std::map<std::string, nlohmann::json> before;
    /// For a delete: every record that pointed at this one, or belonged to it,
    /// as the deleting computer last saw them. Another computer with one it
    /// had not seen, changed or new, keeps the record: an edit, or a use,
    /// wins over a delete.
    std::vector<Referrer> referrers;
};

struct ChangeFile {
    int format = k_change_format;
    /// The schema of the library it was made from.
    int schema = db::k_schema_version;
    std::string library;
    std::string machine;
    std::string computer;
    std::string time;
    std::int64_t sequence = 0;
    std::int64_t counter = 0;
    std::map<std::string, std::int64_t> seen;
    /// The media files the changes bring, by path.
    std::vector<db::MediaDigest> media;
    std::vector<Change> changes;

    [[nodiscard]] db::SyncVersion version() const { return {counter, machine}; }
};

[[nodiscard]] nlohmann::json change_to_json(const Change& change);
/// Throws ``std::runtime_error`` for anything but a change.
[[nodiscard]] Change change_from_json(const nlohmann::json& j);

[[nodiscard]] nlohmann::json change_file_to_json(const ChangeFile& file);

/// Throws ``std::runtime_error`` for a file that is not a change file, and for
/// one of a later format or schema, with a message fit for the operator.
[[nodiscard]] ChangeFile change_file_from_json(const nlohmann::json& j);

/// The file's label, of whatever format: what names it (``format``,
/// ``schema``, ``library``, ``machine``, ``sequence``), who made it and when
/// (``computer``, ``time``), what it was made on top of (``seen``) and what
/// it brings (``media``); no changes. Every
/// format keeps these as format 1 has them, so a SeventhSlide too old to
/// apply a file still checks it, keeps it and passes it on (a server not
/// yet updated). Throws ``std::runtime_error`` for one that is not a change
/// file.
[[nodiscard]] ChangeFile change_file_label(const nlohmann::json& j);

}  // namespace seventhslide::sync
