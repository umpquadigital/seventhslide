#pragma once

// This computer's bookkeeping for syncing a library: tables of the kind
// ``SyncAs::Local``, which never sync.
//
//   - ``sync_shared``: each record as it was last exchanged with the other
//     computers (its *shared copy*), the version of the change that last set
//     each of its fields, and the id it has here. What changed here is what
//     differs from it.
//   - ``sync_applied``: the last change file applied from each other computer.
//   - ``sync_state``: this computer's counter, and the last change file it
//     sent.
//   - ``sync_outbox``: the records marked as written (``sync_outbox.hpp``).
//   - ``sync_held``: changes from other computers that wait for what they
//     need, each with the file it came in.
//   - ``sync_media``: each media file of the library as the change file or
//     snapshot that brought it declared it, its size and SHA-256, and the
//     version of the change that first declared it.
//     A name means the same bytes on every computer for good, so an entry's
//     bytes never change and it never goes (its version only ever comes to
//     the earliest that declared it): a file fetched later is checked against
//     it, and the library's size is counted from it without reading a file.
//
// Seam: Qt SQL via Database.

#include "seventhslide/db/database.hpp"
#include "seventhslide/db/sync_outbox.hpp"

#include <nlohmann/json.hpp>

#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace seventhslide::db {

/// When a change was made, in the only order every computer agrees on: by a
/// counter each computer keeps one past the highest it has seen from anyone,
/// then by the computer's id. Never by the clock, which can be wrong.
struct SyncVersion {
    std::int64_t counter = 0;
    std::string machine;

    friend auto operator<=>(const SyncVersion&, const SyncVersion&) = default;
};

/// ``[counter, "machine"]``.
[[nodiscard]] nlohmann::json version_to_json(const SyncVersion& v);
/// Throws ``std::invalid_argument`` for anything but ``[counter, "machine"]``.
[[nodiscard]] SyncVersion version_from_json(const nlohmann::json& j);

/// The version of each field: ``{"field": [counter, "machine"], ...}``.
[[nodiscard]] nlohmann::json versions_to_json(const std::map<std::string, SyncVersion>& versions);
/// Throws ``std::invalid_argument`` for anything but an object of versions.
[[nodiscard]] std::map<std::string, SyncVersion> versions_from_json(const nlohmann::json& j);

struct SharedCopy {
    /// The record's document as last shared, its parts as fingerprints;
    /// nullopt once the record was deleted.
    std::optional<nlohmann::json> document;
    /// The version that last set each field. A deleted record has one, under
    /// "", the delete's.
    std::map<std::string, SyncVersion> versions;
    /// The id the record has, or had, here (a table that syncs by uid): one
    /// that comes back after a delete takes it again.
    std::optional<std::int64_t> id_here;

    friend bool operator==(const SharedCopy&, const SharedCopy&) = default;
};

/// A record's shared copy, with the record it is of.
struct SharedRecord {
    std::string table;
    std::string key;
    SharedCopy copy;
};

/// A media file as the computer that sent it declared it: its path under the
/// media folder ("images/<name>", ``is_sync_media_path``), its size and the
/// SHA-256 of its bytes, in hex. What a file fetched from any computer is
/// checked against.
struct MediaDigest {
    std::string path;
    std::int64_t size = 0;
    /// The SHA-256 of its pieces' hashes (``sync/media_pieces.hpp``), hex.
    std::string root;
    /// The version of the change that first declared it, where known (kept
    /// here and in snapshots; a change file's are its own): the order the
    /// size limit takes files in.
    std::optional<SyncVersion> version;

    friend bool operator==(const MediaDigest&, const MediaDigest&) = default;
};

/// ``{"path": "images/<name>", "size": 1234, "root": "<hex>"}``, and
/// ``"version": [counter, "machine"]`` when it has one.
[[nodiscard]] nlohmann::json media_digest_to_json(const MediaDigest& m);
/// Throws ``std::invalid_argument`` for anything else: a path that is not a
/// media file's, a fingerprint that is not 64 hex digits.
[[nodiscard]] MediaDigest media_digest_from_json(const nlohmann::json& j);

/// A change from another computer that waits for what it needs: a record it
/// cannot be without, or one it points at.
struct HeldChange {
    std::string table;
    std::string key;
    /// The file it came in: the computer that sent it, and the file's number.
    std::string machine;
    std::int64_t sequence = 0;
    /// The change, as the sync engine keeps it.
    nlohmann::json change;
};

class SyncBookkeeping {
public:
    explicit SyncBookkeeping(Database& db) : db_(db) {}

    [[nodiscard]] std::optional<SharedCopy> shared(std::string_view table, std::string_view key);
    void set_shared(std::string_view table, std::string_view key, const SharedCopy& copy);
    /// The keys of ``table``'s records with a shared copy that is not deleted.
    [[nodiscard]] std::vector<std::string> shared_keys(std::string_view table);
    /// Each shared copy of a table that syncs by record, deleted records'
    /// among them, by table as ``record_tables_in_order`` has them, then by
    /// key: read a row at a time, never all at once.
    void for_each_shared(const std::function<void(SharedRecord)>& take);
    /// For each of ``names`` (a uid, or a file's name), the records, other
    /// than its own, whose shared copy points at the record it names or
    /// belongs to it, as ``names_in`` finds it there (a folder item by its
    /// key, "<folder uid>/<file name>"), as (table, key); a name nothing
    /// points at is left out. Many names read the shared copies once.
    [[nodiscard]] std::map<std::string, std::vector<std::pair<std::string, std::string>>> mentioning(
        const std::set<std::string>& names);

    /// The highest counter this computer has made or seen (0 before any).
    [[nodiscard]] std::int64_t counter();
    void set_counter(std::int64_t counter);
    /// The sequence number of the last change file this computer sent.
    [[nodiscard]] std::int64_t last_sent();
    void set_last_sent(std::int64_t sequence);
    /// The last change file applied from each other computer, by machine id.
    [[nodiscard]] std::map<std::string, std::int64_t> applied();
    void set_applied(std::string_view machine, std::int64_t sequence);

    void hold(const HeldChange& change);
    /// Every held change, by the file it came in.
    [[nodiscard]] std::vector<HeldChange> held();
    /// The held changes to ``table``'s record ``key``, by the file each came in.
    [[nodiscard]] std::vector<HeldChange> held(std::string_view table, std::string_view key);
    /// Each held change, by the file it came in (machine, sequence), then
    /// table and key, a row at a time.
    void for_each_held(const std::function<void(HeldChange)>& take);
    void release(const HeldChange& change);
    /// Release every held change.
    void release_all();

    /// How the media file ``path`` was declared; nullopt for one no change
    /// file or snapshot has declared to this computer.
    [[nodiscard]] std::optional<MediaDigest> media_digest(std::string_view path);
    /// The first declaration of a path stands; a later one is ignored.
    /// Throws ``std::bad_optional_access`` for a digest without its version.
    void set_media_digest(const MediaDigest& digest);
    /// Every media file declared, by path.
    [[nodiscard]] std::vector<MediaDigest> media_digests();
    /// Each media file declared, by path, a row at a time.
    void for_each_media(const std::function<void(MediaDigest)>& take);

    /// Whether ``table``'s record ``key`` is marked written (an output is also
    /// marked by the mark for all of them).
    [[nodiscard]] bool is_marked(std::string_view table, std::string_view key);
    void mark(const MarkedRecord& mark);
    void unmark(const MarkedRecord& mark);
    /// Mark every record: a library put online has everything to send.
    void mark_everything();
    /// Forget every mark: what is here is not this computer's to send.
    void clear_marks();

    /// The library stopped syncing: it is a library of its own from now on,
    /// with a uid of its own, so putting it online again makes a new synced
    /// library rather than a second one under the old library's uid. All
    /// that was kept about what it exchanged goes with it, but the media
    /// declarations, which hold for good.
    void leave_synced_library();
    /// Whether anything is kept here of having synced: what a library never
    /// synced has none of.
    [[nodiscard]] bool synced_before();
    /// As ``leave_synced_library``, the media declarations too: a library put
    /// online starts from what its files are now, whatever build declared
    /// them before.
    void start_afresh();

private:
    [[nodiscard]] std::optional<std::string> state(std::string_view key);
    void set_state(std::string_view key, const std::string& value);

    Database& db_;
};

}  // namespace seventhslide::db
