#pragma once

// The records changed on this computer since they were last shared: the sync
// outbox.
//
// Triggers on every table that syncs mark the record a write touched, in the
// same transaction as the write, so no write path can forget one. A mark says
// only *which* record; what changed is found by comparing the record with the
// copy last shared (``sync_bookkeeping.hpp``), so a save that changed nothing,
// or a change that arrived from another computer, uploads nothing. A record is
// marked once however often it changes: the outbox is never larger than the
// library, and keeps marking whether or not the library syncs.
//
// A mark names its record as the other computers do:
//   - a record table's row by its uid;
//   - a key table's row by its key (a setting's key, a file's name), and a
//     folder item by its folder's uid, a "/", and its filename;
//   - a verse marks its Bible;
//   - an output marks the whole of ``outputs`` (key ""): every save rewrites
//     all of them, and the outputs are told apart by the ids in their JSON.
// A record deleted along with the one it belongs to (a folder's items, a
// Bible's verses) is covered by that record's mark.
//
// Seam: Qt SQL via Database.

#include "seventhslide/db/database.hpp"

#include <compare>
#include <string>
#include <string_view>
#include <vector>

namespace seventhslide::db {

struct MarkedRecord {
    /// The table that names the record (``bibles`` for a verse).
    std::string table;
    std::string key;

    friend auto operator<=>(const MarkedRecord&, const MarkedRecord&) = default;
};

/// Every marked record, by table then key.
[[nodiscard]] std::vector<MarkedRecord> marked_records(Database& db);
/// Every marked record of ``table``, by key.
[[nodiscard]] std::vector<MarkedRecord> marked_records(Database& db, std::string_view table);

}  // namespace seventhslide::db
