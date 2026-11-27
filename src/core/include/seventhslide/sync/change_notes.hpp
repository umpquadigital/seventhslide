#pragma once

// What a change file this computer sent changed, as the shared log notes it
// (``shared_log.hpp``): the records people know, not the tables they are kept
// in. A service's items are the service ("items" changed); a song's verse order
// or theme kept in a style profile is the song's ("verse_order", "theme_map");
// a folder's files, the library's own facts and its settings are not noted,
// nor a record only moved among its neighbours.
//
// The engine keeps a ``SentRecord`` of each change as it sends it (the record
// as it was before is at hand then, and only then); ``change_notes`` turns
// them into the log's items, reading titles it lacks from the library.
//
// Seam: the change file format, Qt SQL via Database.

#include "seventhslide/db/database.hpp"
#include "seventhslide/sync/change_file.hpp"
#include "seventhslide/sync/shared_log.hpp"

#include <nlohmann/json.hpp>

#include <set>
#include <string>
#include <vector>

namespace seventhslide::sync {

struct SentRecord {
    std::string table;
    std::string key;
    bool added = false;
    bool removed = false;
    /// For a change: the fields it changed.
    std::vector<std::string> fields;
    /// What the record is called, from its document.
    std::string title;
    /// A service item's service, by uid.
    std::string service;
    /// A style profile's songs whose verse order, or theme, it changed.
    std::set<std::string> verse_orders;
    std::set<std::string> themes;

    friend bool operator==(const SentRecord&, const SentRecord&) = default;
};

/// ``change`` as sent, ``before`` the record's document as the others had it
/// (null for a new one).
[[nodiscard]] SentRecord sent_record(const Change& change, const nlohmann::json& before);

struct ChangeNotes {
    std::vector<LogItem> items;
    LogCounts counts;
};

/// The records ``sent`` changed, as the log notes them, each once; none when
/// nothing people know changed.
[[nodiscard]] ChangeNotes change_notes(db::Database& db, const std::vector<SentRecord>& sent);

}  // namespace seventhslide::sync
