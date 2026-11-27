#pragma once

// A synced library's shared log: what its computers say to each other, and a
// note of what each changed. Every entry is a file of its own in the store
// (``Numbered::Log``), numbered 1, 2, 3, ... in its computer's place there and
// signed by that computer, as a member list entry is; the link trades them as
// it trades the rest, and lets them through while syncing is paused (something
// live), as they change nothing in the library.
//
//   {
//     "format": 1,
//     "library": "<library uid>",
//     "machine": "<machine id>",
//     "sequence": 4,
//     "order": 31,
//     "time": "2026-11-05T15:21:00Z",
//     "kind": "message",
//     "text": "Sure, I'll go do that!"
//   }
//
//   { ..., "kind": "changes",
//     "items": [{"kind": "song", "key": "<uid>", "title": "In Christ Alone",
//                "action": "changed", "fields": ["lyrics", "verse_order"]}],
//     "counts": {"song": {"changed": 1}} }
//
// ``order`` puts the entries in one order on every computer: each is one
// past the highest of the entries its writer had (each computer's last), so an
// answer always comes after what it answers, whatever the computers' clocks
// say. ``time`` is the writer's clock, for people. A "changes" entry is written
// with each change file a computer sends (``ChangeNotes``): ``items`` names the
// records (the first ``k_most_log_items``), by what they are to people, the
// fields by their columns; ``counts`` counts all of them, by kind and action.
// The words people read are the app's to choose.
//
// Seam: nlohmann/json, the store and the envelope.

#include "seventhslide/crypto/signing.hpp"
#include "seventhslide/sync/identity.hpp"
#include "seventhslide/sync/sync_store.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::sync {

inline constexpr int k_log_format = 1;
/// The longest message, in bytes of UTF-8.
inline constexpr std::size_t k_longest_message = 4000;
/// The most records a "changes" entry names; ``counts`` counts the rest.
inline constexpr std::size_t k_most_log_items = 100;

inline constexpr std::string_view k_log_message = "message";
inline constexpr std::string_view k_log_changes = "changes";

/// A record a "changes" entry names.
struct LogItem {
    /// What it is: "song", "service", "freeform", "web_page", "image",
    /// "video", "audio", "font", "bible", "folder", "output",
    /// "style_profile".
    std::string kind;
    /// Its key, as the change file names it.
    std::string key;
    std::string title;
    /// "added", "changed" or "removed".
    std::string action;
    /// For "changed": the fields changed, by column ("lyrics").
    std::vector<std::string> fields;

    friend bool operator==(const LogItem&, const LogItem&) = default;
};

/// Records counted by kind, then by action.
using LogCounts = std::map<std::string, std::map<std::string, int>>;

struct LogEntry {
    std::string machine;
    std::int64_t sequence = 0;
    std::int64_t order = 0;
    /// ISO 8601, UTC, by the writer's clock.
    std::string time;
    /// ``k_log_message`` or ``k_log_changes``; another a later SeventhSlide
    /// writes is kept, and shown by none.
    std::string kind;
    /// A message's text.
    std::string text;
    std::vector<LogItem> items;
    LogCounts counts;

    friend bool operator==(const LogEntry&, const LogEntry&) = default;
};

[[nodiscard]] nlohmann::json log_entry_to_json(const LogEntry& entry, std::string_view library);

/// A stored entry, read: the entry, the library it says it is of, the format
/// it was written in, and the key that signed it as a computer.
struct ReadLogEntry {
    LogEntry entry;
    std::string library;
    int format = k_log_format;
    crypto::PublicKey signer;
};

/// Read and check ``text``, as the store keeps it. Throws
/// ``std::runtime_error`` when it is not a log entry, or its signature is not
/// its key's. An entry of a later format is read as far as its label
/// (``machine``, ``sequence``, ``order``, ``time``, ``kind``).
[[nodiscard]] ReadLogEntry read_log_entry(std::string_view text);

/// Writing this computer's entries to its place in the store.
class LogBook {
public:
    LogBook(SyncStore& store, Identity me, std::string library);

    /// Post ``text`` (trimmed). Throws ``std::invalid_argument`` when it is
    /// empty or longer than ``k_longest_message``, and ``std::runtime_error``
    /// when it cannot be written.
    LogEntry post_message(std::string_view text);
    /// Note the records a change file sent: the first ``k_most_log_items`` of
    /// ``items``, and ``counts`` of all of them.
    LogEntry post_changes(std::vector<LogItem> items, LogCounts counts);

private:
    LogEntry write(LogEntry entry);

    SyncStore& store_;
    Identity me_;
    std::string library_;
};

/// The entries in a store, each read once: what came since the last look.
class LogReader {
public:
    explicit LogReader(std::string library) : library_(std::move(library)) {}

    /// The entries in ``store`` not read before, of this library. One that
    /// cannot be read is let be, and not read again.
    [[nodiscard]] std::vector<LogEntry> read_new(SyncStore& store);

private:
    std::string library_;
    std::set<std::pair<std::string, std::int64_t>> read_;
};

/// The order entries are shown in: by ``order``, then time, then writer.
[[nodiscard]] bool log_before(const LogEntry& a, const LogEntry& b);

}  // namespace seventhslide::sync
