#pragma once

// A snapshot: the whole library as the computers had shared it at a moment,
// so a computer joining starts from it and replays only the change files that
// came after, not years of them.
//
// It is in parts. Its index is the one file signed, and names its parts by
// the SHA-256 of their bytes:
//
//   {
//     "format": 3, "schema": 12, "library": "<library uid>",
//     "machine": "<machine id>", "computer": "Church PC",
//     "time": "2026-10-24T20:15:00Z", "counter": 240,
//     "files": {"<machine id>": 31, ...},
//     "parts": [{"sha256": "<64 hex>", "size": 4190211}, ...],
//     "records": 8123, "held": 2, "media": 1450
//   }
//
// ``files`` is the change files it holds, from each computer (its own among
// them): 1 to that number. That is always every file each of those was made
// on top of, so the files after it apply to it in order. ``counter`` is the
// highest counter of any of them.
//
// A part, unsigned, holds entries of three kinds:
//
//   {"format": 3,
//    "records": [{"table": "songs", "key": "<uid>", "document": {...},
//                 "versions": {"title": [12, "<machine id>"], ...}},
//                {"table": "songs", "key": "<uid>", "deleted": true,
//                 "versions": {"": [17, "<machine id>"]}}],
//    "held": [{"table": "service_items", "key": "<uid>",
//              "machine": "<machine id>", "sequence": 4, "change": {...}}],
//    "media": [{"path": "images/<name>", "size": 1234, "root": "<hex>",
//               "version": [3, "<machine id>"]}]}
//
// ``records`` is each record with the version of each field, as the change
// files left it (a record deleted, with its delete's version); ``held`` the
// changes in those files still waiting for what they need; ``media`` every
// media file declared to the computer that wrote it (every file those
// records own, a held change's, a deleted record's that an edit may bring
// back), so a computer joining can fetch and check each. Once its hash is
// checked against a signed index a part may come from any computer.
//
// The entries of all the parts, one after another, are in one order: records
// by table (each after the tables it points at,
// ``db::record_tables_in_order``) and key, then held changes by file
// (machine, sequence), table and key, then media by path. ``records``,
// ``held`` and ``media`` in the index count them, which catches a writer's
// mistake. Where a part ends depends on the entries near it, not on what came
// long before (``PartWriter``), so an edit changes the part it is in and the
// next snapshot keeps the rest.
//
// A snapshot is of the records as they were shared (each one's shared copy),
// not as they are here: what is unsent here is left out, and goes in a change
// file after it. So what it holds is exactly what those files make, and a
// computer joining from it holds what every other one does. A shared copy
// keeps only a fingerprint of a record's parts (a Bible's verses), which are
// read from the library: a snapshot waits while a record's parts differ here
// from what was shared.
//
// Seam: nlohmann/json; versions and held changes are db's.

#include "seventhslide/db/schema.hpp"
#include "seventhslide/db/sync_bookkeeping.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <nlohmann/json.hpp>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::sync {

/// The snapshot format this build writes and reads. A snapshot of a later
/// one came from a newer SeventhSlide.
inline constexpr int k_snapshot_format = 3;

struct SnapshotRecord {
    std::string table;
    std::string key;
    /// Nullopt for a record deleted.
    std::optional<nlohmann::json> document;
    std::map<std::string, db::SyncVersion> versions;
};

/// A part as the index names it.
struct SnapshotPartRef {
    std::string sha256;
    std::int64_t size = 0;

    friend bool operator==(const SnapshotPartRef&, const SnapshotPartRef&) = default;
};

struct SnapshotIndex {
    int format = k_snapshot_format;
    int schema = db::k_schema_version;
    std::string library;
    std::string machine;
    std::string computer;
    std::string time;
    std::int64_t counter = 0;
    std::map<std::string, std::int64_t> files;
    std::vector<SnapshotPartRef> parts;
    std::int64_t records = 0;
    std::int64_t held = 0;
    std::int64_t media = 0;

    /// How many change files it holds, all computers' together: its name in
    /// the store (``SnapshotName``).
    [[nodiscard]] std::int64_t file_count() const;

    friend bool operator==(const SnapshotIndex&, const SnapshotIndex&) = default;
};

[[nodiscard]] nlohmann::json snapshot_index_to_json(const SnapshotIndex& index);
/// Throws ``std::runtime_error`` for a file that is not an index (a part
/// named twice among them), and ``NewerFormat`` for one of a later format or
/// schema.
[[nodiscard]] SnapshotIndex snapshot_index_from_json(const nlohmann::json& j);
/// The index's label, of this format or any later one: all but the counts
/// and the counter. Every later format keeps ``parts`` as this one has it, so
/// housekeeping knows which parts any index names.
[[nodiscard]] SnapshotIndex snapshot_index_label(const nlohmann::json& j);

/// What one part holds.
struct SnapshotPart {
    std::vector<SnapshotRecord> records;
    std::vector<db::HeldChange> held;
    std::vector<db::MediaDigest> media;
};

/// Read the part ``text``, checked against ``sha256``, the name an index
/// gives it. Throws ``std::runtime_error`` when it is not that part, or not a
/// part (entries out of order or given twice among them), and
/// ``NewerFormat`` for one of a later format.
[[nodiscard]] SnapshotPart read_snapshot_part(std::string_view text, std::string_view sha256);
/// ``read_snapshot_part`` for ``text`` just checked against its name.
[[nodiscard]] SnapshotPart read_checked_snapshot_part(std::string_view text);

namespace detail {

/// Where an entry falls in the order the parts hold them: kind (record, held,
/// media), then a record's table rank and key, a held change's machine,
/// sequence, table and key, or a media file's path.
struct EntryPlace {
    int kind = 0;
    std::size_t rank = 0;
    std::string first;
    std::int64_t sequence = 0;
    std::string second;
    std::string third;

    friend auto operator<=>(const EntryPlace&, const EntryPlace&) = default;
};

}  // namespace detail

/// An entry too large for a part: a record larger than ``db::k_largest_record``
/// with what comes with it. ``bytes`` is the entry's, as JSON.
struct EntryTooLarge : std::runtime_error {
    EntryTooLarge(const std::string& what, std::int64_t entry_bytes)
        : std::runtime_error(what), bytes(entry_bytes)
    {
    }
    std::int64_t bytes;
};

/// Writes a snapshot's parts from its entries, given in the order the parts
/// hold them, a part at a time: it holds no more than one part.
///
/// A part ends after an entry whose name's hash falls under its share of
/// ``k_snapshot_part_bytes`` (an entry of ``b`` bytes, one time in
/// ``k_snapshot_part_bytes / b``), so parts are of that size on average
/// whatever the entries' sizes, and where one ends depends on the entry, not
/// on where the part began. An entry's name is its table and key (a held
/// change's file first; a media file's path). An entry of
/// ``k_snapshot_part_bytes`` or more is a part of its own, and one that would
/// take a part past ``k_snapshot_part_most`` starts the next. The same
/// entries make the same parts, byte for byte, on any computer.
///
/// A test may give a smaller size in place of ``k_snapshot_part_bytes``
/// (and the most in proportion), so a few records make several parts.
class PartWriter {
public:
    /// Given each part as it is finished, with its bytes. What it throws
    /// stops the writer.
    using Emit = std::function<void(const SnapshotPartRef& part, std::string_view bytes)>;

    /// Throws ``std::invalid_argument`` for a ``target`` under 2 bytes.
    explicit PartWriter(Emit on_part, std::int64_t target = k_snapshot_part_bytes);

    /// Throws ``std::invalid_argument`` for an entry out of order, given
    /// again, or of a table that does not sync by record, and
    /// ``EntryTooLarge`` for a record too large to send
    /// (``db::k_largest_record``).
    void add(const SnapshotRecord& record);
    void add(const db::HeldChange& held);
    /// One without its version throws ``std::invalid_argument``.
    void add(const db::MediaDigest& media);

    /// Finish the last part. The index of what was written: its parts and
    /// counts, the rest for the caller to fill in.
    [[nodiscard]] SnapshotIndex finish();

private:
    /// Add the entry ``text``, its JSON as dumped.
    void add_entry(detail::EntryPlace place, std::string_view name, const std::string& text);
    /// Add ``text``, an entry of ``kind`` (``EntryPlace::kind``), to the part.
    void append(int kind, std::string_view text);
    /// Close the list open in the part, and open each after it to ``kind``'s.
    void open_list(int kind);
    /// Finish the part, if it holds anything, and give it out.
    void cut();

    Emit emit_;
    std::int64_t target_;
    std::int64_t most_;
    /// 2^64 / ``target_``, rounded down: an entry of ``b`` bytes (under
    /// ``target_``) ends its part when its name's hash is under ``b`` of
    /// these, which never passes 2^64.
    std::uint64_t per_byte_;
    std::optional<detail::EntryPlace> last_;
    /// The part so far, as far as the list open in it.
    std::string part_;
    /// The list open in ``part_`` (an ``EntryPlace::kind``); -1 before any.
    int list_ = -1;
    bool empty_list_ = true;
    SnapshotIndex index_;
};

/// Reads a snapshot's parts one after another, as its index names them:
/// each checked against its hash, and the entries of all of them in the one
/// order the parts hold them. ``index`` outlives it.
class PartReader {
public:
    explicit PartReader(const SnapshotIndex& index) : index_(index) {}
    explicit PartReader(SnapshotIndex&&) = delete;

    /// The next part, from ``text``, its bytes. Throws as
    /// ``read_snapshot_part`` does, and ``std::runtime_error`` when its
    /// entries do not come after the last part's, or the index names no more.
    [[nodiscard]] SnapshotPart next(std::string_view text);
    /// Throws ``std::runtime_error`` unless every part was read and the
    /// entries are as many as the index says.
    void finish() const;

private:
    const SnapshotIndex& index_;
    std::size_t read_ = 0;
    std::optional<detail::EntryPlace> last_;
    std::int64_t records_ = 0;
    std::int64_t held_ = 0;
    std::int64_t media_ = 0;
};

}  // namespace seventhslide::sync
